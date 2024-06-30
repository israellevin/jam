#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>

#define MARMALADE_USER "i"
#define NAMESPACE "marmalade"

int elevate() {
    int uid = getuid();
    if (uid == 0) {
        return 0;
    }

    struct passwd *pw = getpwuid(uid);
    if (pw == NULL) {
        perror("getpwuid failed");
        return 1;
    }
    if (strcmp(pw->pw_name, MARMALADE_USER) != 0) {
        fprintf(stderr, "This program should be run by the user '%s'.\n", MARMALADE_USER);
        return 1;
    }
    if (setuid(0) != 0) {
        fprintf(stderr, "This program requires the setuid bit to be set and enabled and has to be owned by root.\n");
        perror("setuid failed");
        return 1;
    }
    return 0;
}

void execute(const char *cmd) {
    fprintf(stderr, "Executing: %s\n", cmd);
    if (system(cmd) != 0) {
        fprintf(stderr, "Failed: %s\n", cmd);
    }
}

int main(int argc, char *argv[]) {
    if (elevate() != 0) {
        return 1;
    }

    if (argc != 7) {
        fprintf(stderr, "Usage: %s "
                "<host interface> <tap interface> <ip address> "
                "<firecracker binary> <firecracker socket> <firecracker config file>\n", argv[0]);
        return 1;
    }
    const char *host_interface = argv[1];
    const char *tap_interface = argv[2];
    const char *ip_address = argv[3];
    const char *firecracker_bin = argv[4];
    const char *firecracker_socket = argv[5];
    const char *firecracker_config = argv[6];

    char cmd[256];

    const char *setup_commands[] = {
        // Enable IP forwarding.
        "sysctl -w net.ipv4.ip_forward=1",

        // Create a network namespace.
        "ip netns add %s",

        // Create a veth pair and move one end into the namespace.
        "ip link add %s.veth0 type veth peer name %s.veth1",
        "ip link set %s.veth1 netns %s",
        "ip addr add 10.200.1.1/24 dev %s.veth0",
        "ip link set %s.veth0 up",

        // Configure the interface in the namespace.
        "ip netns exec %s ip link set lo up",
        "ip netns exec %s ip link set %s.veth1 up",
        "ip netns exec %s ip addr add 10.200.1.2/24 dev %s.veth1",
        "ip netns exec %s ip route add default via 10.200.1.1",

        // Forward packets from the namespace to the default interface.
        "iptables -t nat -A POSTROUTING -s 10.200.1.0/24 -o %s -j MASQUERADE"
    };
    for (int i = 0; i < 11; i++) {
        switch (i) {
            case 0:
                snprintf(cmd, sizeof(cmd), setup_commands[i]);
                break;
            case 1:
            case 4:
            case 5:
            case 6:
                snprintf(cmd, sizeof(cmd), setup_commands[i], NAMESPACE);
                break;
            case 2:
            case 3:
            case 7:
            case 8:
            case 9:
                snprintf(cmd, sizeof(cmd), setup_commands[i], NAMESPACE, NAMESPACE);
                break;
            case 10:
                snprintf(cmd, sizeof(cmd), setup_commands[i], host_interface);
        }
        execute(cmd);
    }

    const char *launch_commande[] = {
        // Create a new tap interface and configure it.
        "ip netns exec %s ip tuntap add %s mode tap",
        "ip netns exec %s ip link set %s up",
        "ip netns exec %s ip addr add %s dev %s",

        // Configure NAT for the Firecracker VM.
        "ip netns exec %s iptables -F",
        "ip netns exec %s iptables -t nat -A POSTROUTING -o %s.veth1 -j MASQUERADE",
        "ip netns exec %s iptables -A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT",
        "ip netns exec %s iptables -A FORWARD -i %s -o %s.veth1 -j ACCEPT",
        "ip netns exec %s %s --api-sock %s --config-file %s"
    };

    for (int i = 0; i < 8; i++) {
        switch (i) {
            case 0:
            case 1:
                snprintf(cmd, sizeof(cmd), launch_commande[i], NAMESPACE, tap_interface);
                break;
            case 2:
                snprintf(cmd, sizeof(cmd), launch_commande[i], NAMESPACE, ip_address, tap_interface);
                break;
            case 3:
            case 5:
                snprintf(cmd, sizeof(cmd), launch_commande[i], NAMESPACE);
                break;
            case 4:
                snprintf(cmd, sizeof(cmd), launch_commande[i], NAMESPACE, NAMESPACE);
                break;
            case 6:
                snprintf(cmd, sizeof(cmd), launch_commande[i], NAMESPACE, tap_interface, NAMESPACE);
                break;
            case 7:
                snprintf(cmd, sizeof(cmd), launch_commande[i], NAMESPACE,
                        firecracker_bin, firecracker_socket, firecracker_config);
                break;
        }
        execute(cmd);
    }
    return 0;
}
