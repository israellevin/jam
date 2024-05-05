# Marmalade - Musical Collaboration Over a Network in Subjective Real-Time

Music is math over time, and time is tricky - doubly so over a network. Marmalade forgoes the illusion of objective simultaneity, and embraces subjective simultaneity instead. Different observers will experience the events of the same musical session in different ways, and possibly in a different order, according to their local time and network, ending up with completely different and individual experiences. Just as they would if they were playing together in the same room.

With Marmalade, there is no central server providing a single source of truth. Instead, Marmalade offers a peer-to-peer protocol for composing sounds in a way that is context aware. Each node in the network can publish these compositions, collect them, synchronize them, and play them in a meaningful and hopefully pleasing way.

## Core Concepts

A Marmalade jam session is made up of Marmalade audio generators which are sent and received by nodes on the Marmalade network, which are referred to as players. Each generator defines a function that can, while a player runs it, produce audio that matches the state of the jam on that player at that point. In addition, generators can register and set certain elements of that state, called tags. The state of the jam on a specific player consists of the sate of the local hardware (clock, sound levels, audio setup, network connectivity), the state of the network the jam is being jammed on (number of participating players and their stats), and the tags set by all the generators that the player is playing.

- `Jam`: a single instance of a musical collaboration session, consisting of a growing collection of generators composed by the participating players.
- `Player`: a single node participating in a jam, running the required software to handle composition, distribution and collection of generators, and the synchronized playback of the sounds they generate. Each player maintains its own version of the state of the jam.
- `State`: the full history of the player, the network and the jam, which is subjective and local to each player. The state is used to determine which sounds to play, when and how.
- `Generator`: the fundamental source of music in a jam - a generator defines a function that produces the right audio for the current state of the jam and sets appropriate tags that other generators can use to make decisions.
- `Tags`: attributes of the state which are set by individual generators.

For example, a player can start a jam by publishing a generator that plays a drum loop indefinitely, or for a specified duration, or one that plays the loop on every 10th second until the last player has left. This generator can also set and alternating "beat" tag in the state, allowing other generator to sync their audio to it.

## Network Protocol

This is the protocol for communication between players. It allows for the following requests to be made (and fulfilled) by any player to any other player in the network:
- `players`: get a list of all players known to the queried player, in reverse chronological order. Each player has a unique name (per jam), an address, and a public key.
- `generators`: get a list of all generators known to the queried player, in reverse chronological order.
- `generator <generator ID>`: get the generator with the specified ID.
- `play <generator ID> <instance ID> <signature> <public key>`: run the generator with the specified ID, but only if it has not been run with the specified instance ID before (this allows for multiple instances of the same generator to be run in parallel).

While the first three calls do not change the state of the jam, and can therefore be anonymous, the `play` call has to be signed by the querying player and verified by the queried player. The `play` call will, in addition to running the generator, download and deploy the generator if it is not locally present and add it to the list of known generators, and add the querying player to the list of players if it is not already there (along with the public key provided, which will be used to verify future `play` calls).

## Player Directory Structure

```
player/
  config.lisp
  generators/
    <jam name:player name:generator name>.tgz
    ...
  jams/
    <jam name>/
      jam.lisp
      jam.log
      jam.aof
      players/
        <player name>/
          generators/
            <generator name>/
              <generator files>
              ...
            ...
          player.lisp
        ...
    ...
```

- `config.lisp` basic initial configurations of the local player, such as the player's name, address, encryption keys and default generator set.
- `generators/` archive of all the generators that the local player has ever known. Each generator has a unique ID made up of the jam's name (which is unique), the name of the player that published it (which is unique per jam), and the generator's name (which is uniuqe to that player in that jam). This directory is the single source of truth for the local player's generators list.
- `jams/` all the jams that the player has ever participated in, each with a unique name and directory.
  - `config.lisp` jam specific configurations, can clobber values in the top level `config.lisp`.
  - `jam.log` debug log of the jam.
  - `jam.aof` append-only file that contains the state of the jam at any given time, as a series of events that have happened in the jam. This is the single source of truth for the state of the jam.
  - `players/` contains all the players that have ever participated in the jam, each with a unique name and directory.
    - `generators/` contains all the generators that the player ran during the jam, each with a unique name and directory extracted from the `generators/` directory.
    - `config.lisp` contains the foreign player's name, address, and public key. Will never clobber anything.

## Generators

To keep generators as agnostic and versatile as possible, we define them as a directory in the player's filesystem, where the first file in the directory will be executed by the player's shell as a separate process with reduced privileges and chrooted to the generator's directory. The player will allow the generator read access to the jam's local state and the ability to produce audio and set tags.

The generator lifecycle is as follows:
1. A generator is composed by a player.
2. The player requests all the players, self included, to `Play` the generator, providing an arbitrary instance ID (current timestamp is a good choice).
3. Every player that receives the request checks that if the generator is locally deployed, `Get`ting it from the requesting player if it is not and deploying it.
4. Every player that receives the request checks if the generator has been run with the specified instance ID before, and if it has not, runs it.
5. The generator instance keeps running until it stops itself, or until the jam ends.

### Read Access to the State

Since the state is essentially a time series, we should give some thought as to how it is updated and queried. We can start with writing every state change event to an append only file and providing the generators with read-only access to that file, but maybe we should start with an off-the-shelf time series databases, possibly one with compatible graphing tools so we can visualize the evolving state of the jam which could be cool.

[Redis](https://redis.io/) seems to have excellent time series support which [Grafana](https://grafana.com/) can visualize, it's fast as hell, setting it up and giving generators read access to it should be a breeze, and requiring that generator developers learn how to query it is a small price to pay.

### Writing Tags to the State

This will be done via an API which the player provides to the generator, so the player stays in charge of implementing the state. We will start with a named pipe in the generator's directory, into which the generator can write JSON objects, with each object containing the full set of current tags and their values.

The ID of the player and the generator will be embedded in the path of the tags, for easy querying and to prevent collisions. In addition, we suggest the following tags for semantic clarity:
- `Composer`: a source of musical creation that composes sounds on a player. May be a person, a group, an algorithem, a set of wind-chimes or anything else that can operate a player. Multiple composers can compose sounds on a single player, and a composer can compose sounds on multiple players. This tag is useful for tracking the source of a sound, and for attributing sounds to their creators.
- `Track`: a musical stream within a jam, grouping together a set of sounds typically associated with a single player and a single composer and representing a single "instrument". This is useful for mixing and muting.
- `Context`: a named timeframe in the jam's schedule in which matching sounds are potentially played (e.g. 'buildup', 'drop', 'verse', 'chorus' or 'bar' - it makes sense to start jams with a basic rhythm generator defining some kind of an alternating 'beat'/'offbeat' context).

### Producing Audio

The generator function should be able to produce any kind of audio, synthesized or sampled - a single note, a bar, a riff, and silences of different durations. One way to acheive this is to give the generator instances access to the player's sound system, and let them write raw audio data to it. This is certainly the most versatile and efficient way to produce audio, but it's also harder to code and debug. Other options include:

- **[Mégra](https://megra-doc.readthedocs.io/en/latest/tutorial/organizing-sound/)** just looks awesome. Requires Jack or PipeWire, but holy shit, it looks awesome.
- **[Supercollider](https://supercollider.github.io/)** is very impressive, seems to be very widely supported and something of an industry standard. Seems to require more setup.
- **[cl-patterns](https://github.com/defaultxr/cl-patterns)** is built on top of Supercollider and does it cooler and lispier.
- **[Sonic Pi](https://sonic-pi.net/)** is something else we can learn from.

For all these options we can use the same named pipe that we use for writing the tags to send audio instructions to the player.

### Schedule

Maybe we create an evolving potential structure of a jam, represented as a directed graph of contexts (or tag collections). The schedule outlines the temporal structure of the jam without specifying the musical content. In many cases it makes sense to start jams with a common schedule for all players, outlining the basic structure of the jam, but as the schedule evolves over time it may end up being different for different players, and it may be interesting to start jams with different schedules for different players.
