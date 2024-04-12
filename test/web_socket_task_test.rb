# frozen_string_literal: true

require "kontena-websocket-client"
require "json"

using_task_library "controldev_websocket"

describe OroGen.controldev_websocket.Task do
    run_live

    attr_reader :task
    attr_reader :websocket

    FIRST_CONNECTION = "new"
    STEAL_CONNECTION = "stolen"
    LOSE_CONNECTION = "lost"
    HANDSHAKE_FAILED = "handshake failed"

    CMD_MSG = {
        axes: Array.new(4, 0),
        buttons: Array.new(16, 0),
        time: 123
    }.freeze

    HANDSHAKE_MSG = {
        test_message: CMD_MSG,
        id: "misc_id"
    }.freeze

    def allocate_interface_port
        server = TCPServer.new(0)
        server.local_address.ip_port
    ensure
        server&.close
    end

    before do
        @task = task = syskit_deploy(
            OroGen.controldev_websocket.Task.deployed_as("websocket")
        )
        @port = allocate_interface_port
        task.properties.port = @port
        task.properties.axis_map = [
            Types.controldev_websocket.Mapping.new(index: 0, type: :Axis),
            Types.controldev_websocket.Mapping.new(index: 1, type: :Axis),
            Types.controldev_websocket.Mapping.new(index: 6, type: :Button),
            Types.controldev_websocket.Mapping.new(index: 2, type: :Axis),
            Types.controldev_websocket.Mapping.new(index: 3, type: :Axis),
            Types.controldev_websocket.Mapping.new(index: 7, type: :Button),
            Types.controldev_websocket.Mapping.new(index: 4, type: :Axis,
                                                   optional: true)
        ]

        task.properties.button_map =
            ((0..5).to_a + (8..14).to_a).map do |i|
                Types.controldev_websocket.ButtonMapping
                     .new(index: i, type: :Button, threshold: 0.5)
            end

        @url = "ws://127.0.0.1:#{@port}/ws"
        @websocket_created = []
    end

    after do
        @websocket_created.each do |s|
            s.ws.close(10) if s.ws&.open?
            flunk("connection thread failed to quit") unless s.connection_thread.join(10)
        end
    end

    describe "connection diagnostics" do
        before do
            syskit_configure_and_start(task)
        end

        it "indicates that there is a pending connection" do
            expect_execution { websocket_create }
                .to do
                    have_one_new_sample(task.statistics_port)
                        .matching { |s| s.pending_connection_id == 1 }
                end
        end

        it "upgrades the connection to controlling once the handshake goes through" do
            socket = websocket_create
            sample =
                expect_execution { websocket_send socket, HANDSHAKE_MSG }
                .to do
                    have_one_new_sample(task.statistics_port)
                        .matching { |s| s.controlling_connection_id == 1 }
                end
            assert_equal 0, sample.pending_connection_id
        end

        it "indicates that there is a pending connection in parallel "\
           "to an existing one" do
            socket = websocket_create
            expect_execution { websocket_send socket, HANDSHAKE_MSG }
                .to do
                    have_one_new_sample(task.statistics_port)
                        .matching { |s| s.controlling_connection_id == 1 }
                end
            s = expect_execution { websocket_create }
                .to do
                    have_one_new_sample(task.statistics_port)
                        .matching { |s| s.pending_connection_id == 2 }
                end

            assert_equal 1, s.controlling_connection_id
        end

        it "successfully indicates a new connection replaced the old one" do
            socket = websocket_create
            expect_execution { websocket_send socket, HANDSHAKE_MSG }
                .to do
                    have_one_new_sample(task.statistics_port)
                        .matching { |s| s.controlling_connection_id == 1 }
                end
            expect_execution { socket = websocket_create }
                .to do
                    have_one_new_sample(task.statistics_port)
                        .matching { |s| s.pending_connection_id == 2 }
                end
            sample = expect_execution { websocket_send socket, HANDSHAKE_MSG }
                .to do
                    have_one_new_sample(task.statistics_port)
                        .matching { |s| s.controlling_connection_id == 2 }
                end

            assert_equal 0, sample.pending_connection_id
        end

        it "indicates that a pending connection closed" do
            socket = nil
            expect_execution { socket = websocket_create }
                .to do
                    have_one_new_sample(task.statistics_port)
                        .matching { |s| s.pending_connection_id == 1 }
                end
            expect_execution { socket.ws.close }
                .to do
                    have_one_new_sample(task.statistics_port)
                        .matching { |s| s.pending_connection_id == 0 }
                end
        end

        it "indicates that the controlling connection closed" do
            socket = websocket_create
            expect_execution { websocket_send socket, HANDSHAKE_MSG }
                .to do
                    have_one_new_sample(task.statistics_port)
                        .matching { |s| s.controlling_connection_id == 1 }
                end
            expect_execution { socket.ws.close }
                .to do
                    have_one_new_sample(task.statistics_port)
                        .matching { |s| s.controlling_connection_id == 0 }
                end
        end
    end

    describe "no maximum time since messages" do
        before do
            syskit_configure_and_start(task)

            @websocket = websocket_create
            message = assert_websocket_receives_message(@websocket)
            assert_state_value_equals message, FIRST_CONNECTION, "connection_state",
                                      "state"
        end

        it "accepts a first connection" do
            assert websocket_running?(@websocket)
        end

        describe "the behavior on new connections when pending" do
            before do
                @second_websocket = websocket_create
            end

            it "closes the first connection cleanly" do
                assert_websocket_closes_cleanly @websocket
                m = assert_websocket_receives_message @websocket
                assert_state_value_equals m, LOSE_CONNECTION, "connection_state", "state"
                assert_state_value_equals m, "pending connection", "connection_state",
                                          "peer"
            end

            it "accepts the second connection" do
                assert websocket_running? @second_websocket
                m = assert_websocket_receives_message @second_websocket
                assert_state_value_equals m, STEAL_CONNECTION, "connection_state", "state"
                assert_state_value_equals m, "pending connection", "connection_state",
                                          "peer"
            end
        end

        describe "the behavior on new connections when controlling" do
            before do
                websocket_send @websocket, HANDSHAKE_MSG
                @second_websocket = websocket_create
                assert websocket_running? @websocket
                assert websocket_running? @second_websocket
                msg = assert_websocket_receives_message @second_websocket
                assert_state_value_equals msg, FIRST_CONNECTION, "connection_state",
                                          "state"
            end

            it "accepts the second handshake" do
                msg = { test_message: { axes: Array.new(4, 0),
                                        buttons: Array.new(16, 0),
                                        time: 123 },
                        id: "misc_id_second" }
                websocket_send @second_websocket, msg

                msg = assert_websocket_receives_message @second_websocket
                assert_state_value_equals msg, STEAL_CONNECTION, "connection_state",
                                          "state"
                assert_state_value_equals msg, "misc_id", "connection_state", "peer"

                assert_websocket_closes_cleanly @websocket
                msg = assert_websocket_receives_message @websocket
                assert_state_value_equals msg, LOSE_CONNECTION, "connection_state",
                                          "state"
                assert_state_value_equals msg, "misc_id_second", "connection_state",
                                          "peer"
            end
        end

        describe "server response is correct" do
            it "does not send a message before receiving one" do
                sleep 0.5
                refute websocket_has_messages?(@websocket)
            end

            it "rejects invalid first messages" do
                websocket_send @websocket, "pineapple"
                msg = assert_websocket_receives_message(@websocket)
                assert_state_value_equals msg, HANDSHAKE_FAILED, "connection_state",
                                          "state"
            end

            it "responds false to a correct control message received before "\
            "a valid handshake message" do
                websocket_send @websocket, CMD_MSG
                msg = assert_websocket_receives_message(@websocket)
                assert_state_value_equals msg, HANDSHAKE_FAILED, "connection_state",
                                          "state"
            end

            it "does not switch to control mode if the received message is not "\
            "a handshake" do
                websocket_send @websocket, CMD_MSG
                assert_websocket_receives_message(@websocket)
                websocket_send @websocket, CMD_MSG
                msg = assert_websocket_receives_message(@websocket)
                assert_state_value_equals msg, HANDSHAKE_FAILED, "connection_state",
                                          "state"
            end

            it "replies to a valid handshake messages" do
                websocket_send @websocket, HANDSHAKE_MSG
                msg = assert_websocket_receives_message(@websocket)
                refute_state_value_equals msg, HANDSHAKE_FAILED, "connection_state",
                                          "state"
            end
        end

        describe "the raw command output on invalid JSON input" do
            it "does not send a message when there is no new JSON input" do
                expect_execution
                    .to { have_no_new_sample task.raw_command_port, at_least_during: 1.0 }
            end

            it "rejects invalid JSON" do
                websocket_send websocket, "pineapple"
                msg = assert_websocket_receives_message(@websocket)
                assert_state_value_equals msg, HANDSHAKE_FAILED, "connection_state",
                                          "state"
            end

            it "rejects valid JSON that is not the expected handshake schema" do
                websocket_send websocket, {}
                msg = assert_websocket_receives_message(@websocket)
                assert_state_value_equals msg, HANDSHAKE_FAILED, "connection_state",
                                          "state"
            end

            it "rejects a valid control message while the handshake was expected" do
                websocket_send websocket, CMD_MSG
                msg = assert_websocket_receives_message(@websocket)
                assert_state_value_equals msg, HANDSHAKE_FAILED, "connection_state",
                                          "state"
            end

            it "rejects invalid JSON after a valid handshake" do
                websocket_send websocket, HANDSHAKE_MSG
                assert_websocket_receives_message(@websocket)

                websocket_send websocket, "pineapple"
                msg = assert_websocket_receives_message(@websocket)
                refute msg.fetch("result")
            end

            it "rejects a valid handshake message during the control phase" do
                websocket_send websocket, HANDSHAKE_MSG
                assert_websocket_receives_message(@websocket)
                websocket_send websocket, HANDSHAKE_MSG
                msg = assert_websocket_receives_message(@websocket)
                refute msg.fetch("result")
            end

            it "rejects an invalid control message after a valid handshake" do
                websocket_send websocket, HANDSHAKE_MSG
                assert_websocket_receives_message(@websocket)

                # missing time field
                msg = { axes: Array.new(2, 0), buttons: Array.new(5, 0) }
                websocket_send websocket, msg
                msg = assert_websocket_receives_message(@websocket)
                refute msg.fetch("result")
            end
        end

        describe "the component's nominal behavior" do
            before do
                websocket_send @websocket, HANDSHAKE_MSG
                msg = assert_websocket_receives_message(@websocket)
                refute_state_value_equals msg, HANDSHAKE_FAILED, "connection_state",
                                          "state"
            end

            it "write raw command samples from JSON messages" do
                expect_execution { websocket_send websocket, CMD_MSG }
                    .timeout(1)
                    .to { have_one_new_sample task.raw_command_port }
            end

            it "does correct axis conversion" do
                msg = {
                    axes: [0.1, 0.2, 0.4, 0.5, 0.7],
                    buttons: Array.new(16, 0), time: 123
                }
                msg[:buttons][6] = 0.3
                msg[:buttons][7] = 0.6

                expected = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7]
                sample =
                    expect_execution { websocket_send websocket, msg }
                    .timeout(1).to { have_one_new_sample task.raw_command_port }

                assert_values_near Array.new(13, 0), sample.buttonValue.to_a
                assert_values_near expected, sample.axisValue.to_a
            end

            it "does correct button conversion" do
                msg = {
                    axes: Array.new(5, 0),
                    buttons: [0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0, 0,
                              0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85],
                    time: 123
                }
                expected = [0, 0, 0, 0, 0, 0,
                            1, 1, 1, 1, 1, 1, 1]
                sample =
                    expect_execution { websocket_send websocket, msg }
                    .timeout(1)
                    .to { have_one_new_sample task.raw_command_port }

                assert_values_near Array.new(7, 0), sample.axisValue.to_a
                assert_values_near expected, sample.buttonValue.to_a
            end

            it "forwards the timestamp" do
                msg = {
                    axes: Array.new(4, 0),
                    buttons: Array.new(16, 0),
                    time: 123_456_789
                }

                sample =
                    expect_execution { websocket_send websocket, msg }
                    .timeout(1)
                    .to { have_one_new_sample task.raw_command_port }

                assert_equal msg[:time], sample.time.to_f * 1000
            end

            it "ignores when an optional axis is not valid" do
                msg = {
                    axes: [0.1, 0.2, 0.3, 0.4],
                    buttons: Array.new(16, 0),
                    time: 123_456_789
                }
                expected = [0.1, 0.2, 0.0, 0.3, 0.4, 0.0, Float::NAN]

                sample =
                    expect_execution { websocket_send websocket, msg }
                    .timeout(1)
                    .to { have_one_new_sample task.raw_command_port }
                assert_equal expected.slice(0, 6), sample.axisValue.to_a.slice(0, 6)
                assert sample.axisValue.to_a[-1].nan?
            end
        end

        describe "the statistics" do
            before do
                websocket_send @websocket, HANDSHAKE_MSG
                msg = assert_websocket_receives_message(@websocket)
                refute_state_value_equals msg, HANDSHAKE_FAILED, "connection_state",
                                          "state"
            end

            it "counts the number of correct messages" do
                sample = expect_execution { websocket_send websocket, CMD_MSG }
                         .timeout(1).to { have_one_new_sample task.statistics_port }

                assert_equal 1, sample.received
                assert_equal 0, sample.errors
                assert_equal 0, sample.too_old
            end

            it "count invalid JSON input" do
                sample = expect_execution { websocket_send websocket, "pineapple" }
                         .timeout(1).to { have_one_new_sample task.statistics_port }

                assert_equal 1, sample.received
                assert_equal 1, sample.errors
                assert_equal 0, sample.too_old
            end

            it "count messages that are missing expected fields" do
                msg = { pine: "apple" }
                sample = expect_execution { websocket_send websocket, msg }
                         .timeout(1).to { have_one_new_sample task.statistics_port }

                assert_equal 1, sample.received
                assert_equal 1, sample.errors
                assert_equal 0, sample.too_old
            end

            it "count messages whose expected fields don't have the expected values" do
                msg = { axes: "pine", buttons: ["apple"] }
                sample = expect_execution { websocket_send websocket, msg }
                         .timeout(1).to { have_one_new_sample task.statistics_port }

                assert_equal 1, sample.received
                assert_equal 1, sample.errors
                assert_equal 0, sample.too_old
            end
        end
    end

    describe "filtering messages by their age" do
        before do
            task.properties.maximum_time_since_message = Time.at(10)
            syskit_configure_and_start(task)

            @websocket = websocket_create
            message = assert_websocket_receives_message(@websocket)
            assert_state_value_equals message, FIRST_CONNECTION, "connection_state",
                                      "state"

            websocket_send @websocket, HANDSHAKE_MSG
            msg = assert_websocket_receives_message(@websocket)
            refute_state_value_equals msg, HANDSHAKE_FAILED, "connection_state", "state"
        end

        it "outputs a command when the message is recent enough" do
            time = seconds_ago(1)
            msg = {
                axes: [0.1, 0.2, 0.3, 0.4, 0.5],
                buttons: Array.new(16, 0),
                time: time.tv_sec * 1e3 + time.tv_usec / 1_000
            }

            expected = [0.1, 0.2, 0.0, 0.3, 0.4, 0.0, 0.5]
            sample =
                expect_execution { websocket_send websocket, msg }
                .timeout(1)
                .to { have_one_new_sample task.raw_command_port }
            assert_equal expected.to_a, sample.axisValue.to_a
        end

        it "doesnt output a command when the message is too old" do
            time = seconds_ago(20)
            msg = {
                axes: [0.1, 0.2, 0.3, 0.4],
                buttons: Array.new(16, 0),
                time: time.tv_sec * 1e3 + time.tv_usec / 1_000
            }
            expect_execution { websocket_send websocket, msg }
                .to { have_no_new_sample task.raw_command_port, at_least_during: 1 }
        end

        it "still outputs statistics when the control command message is old" do
            time = seconds_ago(50)
            msg = {
                axes: [0.1, 0.2, 0.3, 0.4],
                buttons: Array.new(16, 0),
                time: time.tv_sec * 1e3 + time.tv_usec / 1_000
            }
            stats =
                expect_execution { websocket_send websocket, msg }
                .to do
                    have_no_new_sample task.raw_command_port, at_least_during: 1
                    have_one_new_sample task.statistics_port
                end
            assert_equal 1, stats.received
            assert_equal 1, stats.errors
            assert_equal 1, stats.too_old
        end
    end

    def seconds_ago(seconds)
        Time.now - seconds
    end

    def websocket_state_struct
        @websocket_state_struct ||= Concurrent::MutableStruct.new(
            :ws, :received_messages, :connection_thread
        )
    end

    def websocket_running?(state)
        state.connection_thread.alive?
    end

    def websocket_success?(state)
        return false if websocket_running?(state)

        begin
            state.connection_thread.value
            true
        rescue RuntimeError
            false
        end
    end

    def websocket_send(state, msg)
        msg = JSON.generate(msg) unless msg.respond_to?(:to_str)
        state.ws.send(msg)
    end

    def websocket_has_messages?(state)
        !state.received_messages.empty?
    end

    def assert_websocket_closes_cleanly(state, timeout: 3)
        deadline = Time.now + timeout
        while Time.now < deadline
            return unless state.ws.connected?

            sleep 0.1
        end
        flunk("websocket did not close in #{timeout}s")
    end

    def assert_websocket_receives_message(state, timeout: 3)
        deadline = Time.now + timeout
        while Time.now < deadline
            unless state.received_messages.empty?
                return JSON.parse(state.received_messages.pop)
            end

            sleep 0.1
        end
        flunk("no messages received in #{timeout}s")
    end

    def websocket_connect(state, event)
        Kontena::Websocket::Client.connect(@url) do |ws|
            state.ws = ws
            event&.set
            ws.read do |message|
                state.received_messages += [message]
            end
        end
    rescue Kontena::Websocket::CloseError # rubocop:disable Lint/SuppressedException
    ensure
        event&.set
    end

    def websocket_create(wait: true)
        s = websocket_state_struct.new(nil, [], nil)
        event = Concurrent::Event.new if wait
        s.connection_thread = Thread.new { websocket_connect(s, event) }

        if event && !event.wait(5)
            unless s.connection_thread.status
                s.connection_thread.value # raises if the thread terminated with exception
            end
            flunk("timed out waiting for the websocket to connect")
        end
        @websocket_created << s

        s
    end

    def assert_state_value_equals(hash, expected, *path)
        actual = path.inject(hash) { |h, p| h.fetch(p) }
        assert_equal expected, actual
    end

    def refute_state_value_equals(hash, expected, *path)
        actual = path.inject(hash) { |h, p| h.fetch(p) }
        refute_equal expected, actual
    end

    # Helper that allows compating values within a recursive hash (i.e. JSON doc)
    def assert_values_near(expected, actual, tolerance: 1e-4) # rubocop:disable Metrics/AbcSize
        if expected.length != actual.length
            flunk("expected length #{expected.length}, got #{actual.length}")
        end

        expected.each_index do |ind|
            value = expected[ind]
            v = actual[ind]
            if value.kind_of?(Numeric)
                flunk("expected #{v.inspect} to be a number") unless v.kind_of?(Numeric)
                assert_in_delta value, v, tolerance
            else
                flunk("expected value #{value.inspect} should be a number")
            end
        end
    end
end
