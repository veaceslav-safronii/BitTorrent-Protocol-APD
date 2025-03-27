# Apd-Homework2 : BitTorrent Protocol

## Data Structures:

- `File`: contains the name of the file, number of segments and the segments (as an array of strings).
- `ClientData`: contains the information about a client, its owned files, files to be downloaded and a termination flag.
- `Swarm`: contains a file and the list of peers that have that file
- `TrackerData`: contains the information about a tracker, that includes the list of swarms and a counter of completed clients.

## Tracker

### Initialization

1. Waits for a message from each client, which contains the list of owned files with its segments.
2. Saves all the files in a list of swarms and for each file adds the respective client in the list of peers.
3. Once all clients have sent their initial messages, the tracker sends to each client an "ACK" to signal that clients can begin downloading the desired files.

### Receiving Messages from Clients

When the tracker receives a message from a client, it performs the following steps:

1. Checks the tag type of the receive message.
2. Depending on the type, performs one of the following actions:
    - If the message is a request for a file (tag 1), the tracker provides the client with the number of segments and the segment hashes, and marks the client as a peer for that file.
   - If the message is a request for the list of peers (tag 2), the tracker responds with the list of peers for the requested file.
   - If the message indicates the completion of all file downloads by a client (tag 5), the tracker increments the counter of completed clients.
   - The loop continues until all clients have completed downloading all files.
3. Sents a "TERMINATE" message to all the clients.
4. Frees the dynamicly allocated memory.

## Clients

### Initialization

1. Reads the input file using `read_input_file` function.
2. Sends the tracker the information about the owned files.
3. Waits for a response from the tracker.


### Download

Once the client receives confirmation from the tracker, it performs the following steps:

1. Requests from the tracker the number of segments for a file and the segments themselves.
2. Requests for each 10 segments the list of peers that contain the according file.
3. Uses the Round-Robin algorithm for each segments to traverse the list of peers.
4. Sends a request with a segment to a peer.
5. Waits to receive the response from that peer.
6. If the response is "ACK" then the client adds this segment to the file stored in a local list, incrementing the number of downloaded segments.

- Once all the segments of a file were downloaded, the client writes the segments to appropriate output file and frees the dynamicly allocated memory.

- When all the files were downloaded, the client signals the tracker of finishing downloading.

### Upload

When a client receives a segment request from a peer, it performs the following steps:

1. Checks if it has the requested segment.
2. If it has the segment, it sends an "ACK" message back to the peer.
3. If it does not have the segment, it sends a "NOK" message back to the peer.
4. Continues to listen for more segment requests until it receives a termination signal from the tracker.







