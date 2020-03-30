# frozen_string_literal: true

require 'kontena-websocket-client'
require 'json'

using_task_library 'controldev'

WebsocketStruct = Concurrent::MutableStruct.new :ws,
                  :received_messages, :current_state, :has_error

def websocket_connect(state)
    Kontena::Websocket::Client.connect(@url) do |ws|
        state.ws = ws
        state.current_state = :open

        ws.read do |message|
            state.received_messages.append(message)
        end
    end
    rescue Kontena::Websocket::CloseError => exc
        state.current_state = :closed
    rescue Kontena::Websocket::Error => exc
        state.has_error = true
end

describe OroGen.controldev.WebSocketTask do
    run_live

    def start_websocket (name)
        $first_websockets[name] = WebsocketStruct.new(nil, [], :pending, false)

        thread = Thread.new {
            websocket_connect(name)
        }
    end

    before do
        @task = task = syskit_deploy(
            OroGen.controldev.WebSocketTask.deployed_as('websocket')
        )
        task.properties.port = 65432
        @url = 'ws://localhost:65432/ws'

        syskit_configure(task)

        expect_execution { task.start! }.
            to { emit task.start_event}

        s = @first_websocket = WebsocketStruct.new(nil, [], :pending, false)
        expect_execution {
                Thread.new { websocket_connect(s) }
            }.
            to { achieve {s.current_state == :open} }
    end

    after do
        if (@first_websocket.current_state != :closed)
            # close the connection cleanly
            s = @first_websocket
            expect_execution { s.ws.close(1000) }.
            to { achieve { s.current_state  == :closed } }
        end

        assert !@first_websocket.has_error

        task = @task
        expect_execution { task.stop! }.
            to { emit task.stop_event}
    end

    describe 'server accept first connection' do
        it 'connection is still open' do
            assert_equal :open, @first_websocket.current_state
        end

        it 'connection didn\'t failed' do
            assert !@first_websocket.has_error
        end
    end

    describe 'server closes first connection when accepting second' do
        before do
            s = @second_websocket = WebsocketStruct.new(nil, [], :pending, false)
            expect_execution {
                    Thread.new { websocket_connect(s) }
                }.
                to { achieve {s.current_state == :open} }
        end

        after do
            # close the connection cleanly
            s = @second_websocket
            expect_execution { s.ws.close(1000) }.
            to { achieve { s.current_state  == :closed } }

            assert !@second_websocket.has_error
        end

        it 'first connection is closed' do
            s = @first_websocket
            expect_execution.to { achieve { s.current_state  == :closed } }
        end

        it 'second connection is still open' do
            assert_equal :open, @second_websocket.current_state
        end

        it 'both connection didn\'t failed' do
            assert !@first_websocket.has_error and !@second_websocket.has_error
        end
    end

    describe 'server response is correct' do
        it 'didn\'t response any message before receiving one' do
            assert !@first_websocket.received_messages.any?
        end

        it 'responses false to wrong messages' do
            s = @first_websocket
            expect_execution{ s.ws.send('pineapple') }.
                to { achieve { s.received_messages.any? } }

            assert !JSON.parse(s.received_messages.pop)["result"]
        end

        it 'reponses true to correct messages' do
            msg = { :axes => Array.new(4, 0),
                    :buttons => Array.new(16, 0),
                    :status => false }

            s = @first_websocket
            expect_execution{ s.ws.send(JSON.generate(msg)) }.
                to { achieve { s.received_messages.any? } }

            assert JSON.parse(s.received_messages.pop)["result"]
        end
    end

    describe 'server write at the port correctely' do
        it 'doesn\'t write when receiving nothing' do
            task = @task
            expect_execution.
                to { have_no_new_sample task.raw_command_port, at_least_during: 1.0 }
        end

        it 'doesn\'t write when receiving wrong messages' do
            s = @first_websocket
            task = @task
            expect_execution { s.ws.send('pineapple') }.
                to { have_no_new_sample task.raw_command_port, at_least_during: 1.0 }
        end

        it 'doesn\'t write when not controlling' do
            s = @first_websocket
            task = @task
            msg = { :axes => Array.new(4, 0),
                    :buttons => Array.new(16, 0),
                    :status => false }
            expect_execution { s.ws.send(JSON.generate(msg)) }.
                to { have_no_new_sample task.raw_command_port, at_least_during: 1.0 }
        end

        it 'writes when is controlling' do
            s = @first_websocket
            task = @task
            msg = { :axes => Array.new(4, 0),
                    :buttons => Array.new(16, 0),
                    :status => true }
            sample = expect_execution { s.ws.send(JSON.generate(msg))}.timeout(1).
                to { have_one_new_sample task.raw_command_port }
        end

        it 'do correct axis conversion' do
            s = @first_websocket
            task = @task
            msg = { :axes => [-0.1, -0.2, -0.4, -0.5],
                    :buttons => Array.new(16, 0),
                    :status => true }
            msg[:buttons][6] = 0.35
            msg[:buttons][7] = 0.2
            msg[:buttons][15] = 0.1
            msg[:buttons][14] = 0.8
            msg[:buttons][13] = 0.1
            msg[:buttons][12] = 0.9
            expected = [-0.1, -0.2, -0.3, -0.4, -0.5, -0.6, -0.7, -0.8]
            sample = expect_execution { s.ws.send(JSON.generate(msg))}.timeout(1).
                to { have_one_new_sample task.raw_command_port }

            assert_values_near Array.new(8, 0), sample.buttonValue.to_a

            assert_values_near expected, sample.axisValue.to_a
        end
    end
end

# Helper that allows compating values within a recursive hash (i.e. JSON doc)
def assert_values_near(expected, actual, tolerance: 1e-4)
    if expected.length != actual.length
        flunk("expected length #{expected.length}, got #{actual.length}")
    end

    expected.each_index do |ind|
        value = expected[ind]
        v = actual[ind]
        if value.kind_of?(Numeric)
            unless v.kind_of?(Numeric)
                flunk("expected #{v.inspect} to be a number")
            end
            assert_in_delta value, v, tolerance
        else
            flunk("expected value #{value.inspect} should be a number")
        end
    end
end