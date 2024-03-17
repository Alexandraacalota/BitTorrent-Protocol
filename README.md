Copyright 2024 Alexandra-Maria Calota
# BitTorrent Protocol

## Overview

The project presents a simulation of the BitTorrent Protocol using MPI, facilitating communication between multiple processes to emulate tracker and client functionalities. Each client comprises two threads, dedicated to download and upload operations.

## Implementation

### Protocol Entities

- **Client, Swarm, and Tracker Definitions:**
    - Client: Contains the 2D array of file hashes, representing files owned by the client.
    - Swarm: Includes an array of clients and details of the file, such as name and size.
    - Tracker: Manages swarms owned by all clients, coordinating file transfers.

- **Thread Function Arguments:**
    - `thread_func_argument` type is utilized for the argument to `download_thread_func` to pass necessary information from the peer function to the download thread.

### Tracker Functionality

- **Initialization:**
    - The tracker waits for all clients to send their initially owned files, storing swarm information upon receipt.
    - It handles duplicate file checks and updates the swarm accordingly.
    - After initialization, the tracker signals clients to commence downloading.

- **Download Management:**
    - Monitors ongoing downloads, processing requests for swarms and handling client updates.
    - Manages different message tags to facilitate swarm requests, completion notifications, and file updates.
    - Initiates shutdown procedures once all downloads are complete.

### Client Functionality

- **Peer Function:**
    - Reads input files to obtain file details and desired downloads.
    - Initiates download and upload threads for file transfer operations.

- **Download Thread:**
    - Communicates with the tracker to request swarms and segments from other clients.
    - Utilizes random selection to distribute segment download requests among available clients.
    - Handles segment downloads, file completion, and update notifications.

- **Upload Thread:**
    - Receives and processes messages, acknowledging segment downloads from other clients.
    - Terminates upon receiving signals from the tracker.

## Conclusion

The BitTorrent Protocol implementation leveraging MPI offers a comprehensive simulation environment for file sharing. Through coordinated interactions between tracker and clients, the system demonstrates efficient swarm management, segment distribution, and download completion. This project showcases the effectiveness of parallel and distributed algorithms in emulating complex network protocols.
