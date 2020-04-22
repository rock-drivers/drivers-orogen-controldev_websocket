# Controldev Websocket

A websocket-based proxy for controldev commands, and associated javascript to read the data from the Gamepad API and send it there.

## Task

This component just have one task, called `Task`, which is the websocket server. The server receives a JSON message and tries to build a `controldev/RawCommand` to send over `raw_command` port.

Also, the first message sent by the websocket client should be handshake with a JSON containing just a `test_message` with the original command inside it like:

```
{
    "test_message": {
        "buttons": { ... }
        "axes": { ... }
    }
}
```

In addition, the task has a `statistics` port sending `controldev_websocket/Statistics` with `received` and `errors` fields counting incoming messages and failures to build a `controldev/RawCommand` with it.

## Configuration

The `Task` has 3 properties:
- `port`: Is the server port, default to 58000.
- `axis_map`: Is the axis mapping from WebSocket `axes` to `RawCommand.axisValue`. Type is a vector of `controldev_websocket/Mapping`.
- `button_map`: Is the button mapping from WebSocket `buttons` to `RawCommand.buttonValue`. Type is a vector of `controldev_websocket/ButtonMapping`.

The `default` configuration used in the example is:
```
--- name:default

axis_map:
  - index: 0
    type: Axis
  - index: 1
    type: Axis
  - index: 6
    type: Button
  - index: 2
    type: Axis
  - index: 3
    type: Axis
  - index: 7
    type: Button

button_map:
  - index: 0
    type: Button
    threshold: 0.5
  - index: 1
    type: Button
    threshold: 0.5
  - index: 2
    type: Button
    threshold: 0.5
  - index: 3
    type: Button
    threshold: 0.5
  - index: 4
    type: Button
    threshold: 0.5
  - index: 5
    type: Button
    threshold: 0.5
  - index: 8
    type: Button
    threshold: 0.5
  - index: 9
    type: Button
    threshold: 0.5
  - index: 10
    type: Button
    threshold: 0.5
  - index: 11
    type: Button
    threshold: 0.5
  - index: 12
    type: Button
    threshold: 0.5
  - index: 13
    type: Button
    threshold: 0.5
  - index: 14
    type: Button
    threshold: 0.5
```

## Usage

At the examples folder has the necessary files to run the component (server) within a bundle and a simple HTML page to run the client GamepadStream object.

Note, the GamepadStream object is defined in (gamepad_stream.ts)[./examples/gamepad_stream.ts] file and then the TypeScript code is 'compiled' using `tsc gamepad_stream.ts` command resulting the (gamepad_stream.js)[./examples/gamepad_stream.js] used at the HTML file.

To run the server just inside a bundle run (you might have the config file at this bundle):
```
syskit run ../../drivers/orogen/controldev_websocket/examples/run.rb
```

Then, simply open the (stream.html)[examples/stream.html] in your browser, connect a gamepad and enjoy it. To run it from another computer replace the `localhost` in HTML file with the server IP address.