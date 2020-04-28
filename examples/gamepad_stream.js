var Status;
(function (Status) {
    Status["connecting"] = "connecting";
    Status["start_control"] = "waiting key";
    Status["controlling"] = "controlling";
    Status["disconnected"] = "disconnected";
    Status["connection_error"] = "connection_error";
    Status["gamepad_disconnected"] = "gamepad_disconnected";
})(Status || (Status = {}));
var GamepadStream = /** @class */ (function () {
    function GamepadStream(url, gamepad_index) {
        // Tolerance of server failures when processing the messages.
        this.fail_tolerance = 10;
        // The streaming sending rate (Hz). Default is 5 Hz (200ms) the human reaction time
        this.rate = 5;
        this.running = false;
        this.period = 0.1;
        this.start_button = 0;
        this.fail_count = 0;
        // The status encapusulates the WebSocket and the Gamepad connection status.
        this.status = Status.connecting;
        if (!navigator.getGamepads)
            throw "This browser doesn't support gamepads!";
        this.websocket = new WebSocket(url);
        var self = this;
        this.websocket.onopen = function () { self.onOpen(); };
        this.websocket.onclose = function () { self.onClose(); };
        this.websocket.onerror = function (err) { self.onError(err); };
        this.websocket.onmessage = function (msg) { self.onMessage(msg); };
        this.gamepad_index = gamepad_index;
        if (!this.isGamepadConnected())
            throw "The gamepad provided is not available";
    }
    GamepadStream.getFirstValidGamepad = function (url) {
        console.log("Trying to find a gamepad...");
        var gamepads = navigator.getGamepads();
        for (var i = 0; i < gamepads.length; i++) {
            if (gamepads[i] === null || !gamepads[i].connected)
                continue;
            console.log("Gamepad sucefully conected!");
            console.log("    Index: " + gamepads[i].index);
            console.log("    ID: " + gamepads[i].id);
            console.log("    Axes: " + gamepads[i].axes.length);
            console.log("    Buttons: " + gamepads[i].buttons.length);
            console.log("    Mapping: " + gamepads[i].mapping);
            console.log("\n");
            return new GamepadStream(url, i);
        }
        throw "Bad things happened, gamepad not found.";
    };
    GamepadStream.prototype.start = function (rate) {
        if (!this.running) {
            if (rate != undefined)
                this.rate = rate;
            var self = this;
            this.poll = setInterval(function () { self.updateStatus(); }, 1000 / this.rate);
        }
    };
    GamepadStream.prototype.stop = function () {
        if (this.running)
            clearInterval(this.poll);
    };
    GamepadStream.prototype.gamepad = function () {
        return navigator.getGamepads()[this.gamepad_index];
    };
    GamepadStream.prototype.isGamepadConnected = function () {
        if (this.gamepad() === null) {
            return false;
        }
        return this.gamepad().connected;
    };
    GamepadStream.prototype.buildMessage = function () {
        var msg = { 'axes': this.gamepad().axes,
            'buttons': this.gamepad().buttons.map(function f(b) { return b.value; })
        };
        return msg;
    };
    GamepadStream.prototype.sendGamepadMessage = function () {
        if (this.getStatus() != Status.controlling)
            throw "Tried to send a message when is not controlling";
        var msg = this.buildMessage();
        this.websocket.send(JSON.stringify(msg));
    };
    GamepadStream.prototype.askControl = function () {
        if (this.getStatus() != Status.start_control)
            throw "Tried to ask control when is not starting the control";
        var msg = { 'test_message': this.buildMessage() };
        this.websocket.send(JSON.stringify(msg));
    };
    // Do the transitioning between the machine states and update it,
    GamepadStream.prototype.updateStatus = function () {
        switch (this.getStatus()) {
            case Status.connecting:
                console.log("waiting connection");
                break;
            case Status.gamepad_disconnected:
                if (this.isGamepadConnected())
                    this.status = Status.start_control;
                break;
            case Status.start_control:
                if (this.gamepad().buttons[this.start_button].value == 1)
                    this.askControl();
                break;
            case Status.controlling:
                this.sendGamepadMessage();
                break;
            case Status.disconnected:
                this.stop();
        }
    };
    GamepadStream.prototype.getStatus = function () {
        switch (this.websocket.readyState) {
            case 0:// Socket has been created. The connection is not yet open.
                this.status = Status.connecting;
                return this.status;
            case 1:// The connection is open and ready to communicate.
                if (!this.isGamepadConnected())
                    this.status = Status.gamepad_disconnected;
                return this.status;
            default:// The connection is closing, closed or couldn't be opened.
                this.status = Status.disconnected;
                return this.status;
        }
    };
    GamepadStream.prototype.onOpen = function () {
        this.status = Status.start_control;
        console.log('Gamepad websocket connected');
    };
    GamepadStream.prototype.onClose = function () {
        this.status = Status.disconnected;
        console.log('Gamepad websocket disconnected');
    };
    GamepadStream.prototype.onError = function (err) {
        console.log(err);
        this.status = Status.connection_error;
    };
    GamepadStream.prototype.onMessage = function (msg) {
        if (!JSON.parse(msg.data).result) {
            this.fail_count++;
            if (this.fail_count > this.fail_tolerance) {
                this.status = Status.disconnected;
                this.websocket.close();
                alert("Vessel failed to process joystick commands, disconnected.");
            }
        }
        else {
            this.fail_count = 0;
            if (this.status == Status.start_control || this.status == Status.connection_error)
                this.status = Status.controlling;
        }
    };
    return GamepadStream;
}());
