using_task_library 'controldev_websocket'

Syskit.conf.use_deployment OroGen.controldev_websocket.Task => 'gamepad_socket'#, valgrind: true

Robot.controller do
    # To work properly a configuration can be made based on the property set in test file.
    Roby.plan.add_mission_task OroGen.controldev_websocket.Task.with_conf("default")
end
