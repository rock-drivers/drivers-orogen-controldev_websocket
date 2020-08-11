function addListItem(ul, text) {
    li = document.createElement("li");
    li.innerHTML = text;
    ul.appendChild(li);
}

function formatGamepadState(gamepad) {
    gamepad_info = document.createElement("ul");
    addListItem(gamepad_info, `Index: ${gamepad.index}`)
    addListItem(gamepad_info, `Connected: ${gamepad.connected}`)
    addListItem(gamepad_info, `Display ID: ${gamepad.displayId}`)
    addListItem(gamepad_info, `ID: ${gamepad.id}`)
    addListItem(gamepad_info, `Mapping: ${gamepad.mapping}`)
    addListItem(gamepad_info, `Timestamp: ${gamepad.timestamp}`)

    axes = document.createElement("ul");
    gamepad.axes.forEach((axis, i) => {
        addListItem(axes, `axes[${i}] = ${axis}`)
    })

    buttons = document.createElement("ul");
    gamepad.buttons.forEach((button, i) => {
        addListItem(buttons, `buttons[${i}] = { value: ${button.value}, pressed: ${button.pressed} }`)
    })

    button_item = document.createElement("li");
    gamepad_info.appendChild(button_item)
    button_item.appendChild(buttons)

    axes_item = document.createElement("li");
    gamepad_info.appendChild(axes_item)
    axes_item.appendChild(axes)

    return gamepad_info
}

function createGamepadWatch() {
    window.setInterval(function() {
        body = document.body;
        document.body.innerHTML = "";
        for (let i = 0; i < navigator.getGamepads().length; ++i) {
            let gamepad = navigator.getGamepads()[i];
            if (gamepad !== null) {
                body.appendChild(formatGamepadState(gamepad))
            }
            else {
                gamepad_info = document.createElement("div");
                gamepad_info.innerHTML = `Gamepad [${i}] is null`
                body.appendChild(gamepad_info)
            }
        }
    }, 100)
}
