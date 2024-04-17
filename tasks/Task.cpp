/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"

#include <base-logging/Logging.hpp>

#include <seasocks/PrintfLogger.h>
#include <seasocks/Server.h>
#include <seasocks/WebSocket.h>

#include <iostream>
#include <json/json.h>
#include <string.h>

#include <future>
#include <chrono>

#include <list>

using namespace std;
using namespace controldev_websocket;
using namespace controldev;
using namespace seasocks;
using namespace std::chrono_literals;

struct controldev_websocket::Client {
    WebSocket* connection = nullptr;
    std::string id = "";
    Json::FastWriter fast;
    uint16_t connection_id = 0;

    bool isEmpty()
    {
        return connection == nullptr;
    }
};
struct controldev_websocket::JoystickHandler : WebSocket::Handler {
    Client pending;
    Client controlling;
    uint16_t connection_id = 0;
    Task* task = nullptr;
    Json::FastWriter fast;
    Statistics statistic;

    void handleNewConnection(Client newer, Client other)
    {
        Json::Value feedback;
        if (other.isEmpty()) {
            feedback["connection_state"]["state"] = "new";
        }
        else {
            feedback["connection_state"]["state"] = "lost";
            feedback["connection_state"]["peer"] =
                newer.id.empty() ? "pending connection" : newer.id;
            other.connection->send(fast.write(feedback));
            other.connection->close();
            feedback["connection_state"]["state"] = "stolen";
            feedback["connection_state"]["peer"] =
                other.id.empty() ? "pending connection" : other.id;
        }
        newer.connection->send(fast.write(feedback));
    };

    void onConnect(WebSocket* socket) override
    {
        Client new_pending;
        new_pending.connection = socket;
        new_pending.connection_id = ++connection_id;
        handleNewConnection(new_pending, pending);
        pending = new_pending;
        task->m_statistics.pending_connection_id = pending.connection_id;
        task->outputStatistics();
    }
    void onData(WebSocket *socket, const char *data) override
    {
        if (socket != pending.connection && socket != controlling.connection) {
            LOG_WARN_S << "Received message from inactive connection" << std::endl;
            return;
        }
        bool result = task->parseIncomingWebsocketMessage(data, socket);

        if (socket == pending.connection) {
            LOG_WARN_S << "Handling Ask Control Message" << std::endl;
            result = result && task->getIdFromMessage(pending.id);
            result = result && task->handleAskControlMessage();
            if (result) {
                handleNewConnection(pending, controlling);
                controlling = pending;
                pending = Client();
                task->m_statistics.pending_connection_id = 0;
                task->m_statistics.controlling_connection_id = controlling.connection_id;
                task->outputStatistics();
                LOG_WARN_S << "Control given to the id " << controlling.id << std::endl;
                return;
            }

            LOG_WARN_S << "Handshake with id " << pending.id << " failed" << std::endl;
            Json::Value msg;
            msg["connection_state"]["state"] = "handshake failed";
            socket->send(fast.write(msg));
            return;
        }
        result = result && task->handleControlMessage();
        Json::Value msg;
        msg["result"] = result;
        ++task->m_statistics.received;

        // Increment errors count if the result is false, do nothing otherwise
        task->m_statistics.last_received_message_time = task->getLastMessageTime();
        task->m_statistics.errors += result ? 0 : 1;

        task->outputStatistics();
        socket->send(fast.write(msg));
    }
    void onDisconnect(WebSocket* socket) override
    {
        if (controlling.connection == socket) {
            controlling = Client();
            task->m_statistics.controlling_connection_id = 0;
            task->outputStatistics();
        }
        else if (pending.connection == socket) {
            pending = Client();
            task->m_statistics.pending_connection_id = 0;
            task->outputStatistics();
        }
        else {
            LOG_WARN_S << "Received message from inactive connection" << std::endl;
        }
    }
};

struct controldev_websocket::MessageDecoder {
    Json::Value jdata;
    Json::CharReaderBuilder rbuilder;
    std::unique_ptr<Json::CharReader> const reader;

    MessageDecoder()
        : reader(rbuilder.newCharReader())
    {
    }

    static std::string mapFieldName(Mapping::Type type)
    {
        if (type == Mapping::Type::Button) {
            return "buttons";
        }
        if (type == Mapping::Type::Axis) {
            return "axes";
        }
        throw "Failed to get Field Name. The type set is invalid";
    }

    bool parseJSONMessage(char const* data, std::string& errors)
    {
        return reader->parse(data, data + std::strlen(data), &jdata, &errors);
    }

    bool validateControlMessage()
    {
        if (!jdata.isMember(mapFieldName(Mapping::Type::Axis)) ||
            !jdata.isMember(mapFieldName(Mapping::Type::Button)) ||
            !jdata.isMember("time")) {
            LOG_ERROR_S << "Invalid message, it doesn't contain the required fields.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    bool validateAskControlMessage()
    {
        if (!jdata.isMember("test_message")) {
            LOG_ERROR_S << "Invalid test msg, it doesn't contain the test_message field.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        jdata = jdata["test_message"];
        return validateControlMessage();
    }

    bool validateId()
    {
        if (!jdata.isMember("id")) {
            LOG_ERROR_S << "Invalid test msg, it doesn't contain the id field.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    double getValue(const Mapping& mapping)
    {
        auto const& field = jdata[mapFieldName(mapping.type)];
        if (!field.isValidIndex(mapping.index)) {
            if (!mapping.optional) {
                throw std::out_of_range("incoming raw command does not have a field " +
                                        std::to_string(mapping.index) + "in " +
                                        mapFieldName(mapping.type));
            }
            return base::NaN<double>();
        }
        return field[mapping.index].asDouble();
    }

    std::string getId()
    {
        auto const& field = jdata["id"];
        return field.asString();
    }

    base::Time getTime()
    {
        return base::Time::fromMilliseconds(jdata["time"].asUInt64());
    }
};

bool Task::parseIncomingWebsocketMessage(char const* data, WebSocket* connection)
{
    std::string errs;
    if (!m_decoder->parseJSONMessage(data, errs)) {
        LOG_ERROR_S << "Failed parsing the message, got error: " << errs << std::endl;
        return false;
    }
    return true;
}

bool Task::handleControlMessage()
{
    if (!m_decoder->validateControlMessage()) {
        return false;
    }

    auto message_time = m_decoder->getTime();
    auto time_since_message = base::Time::now() - message_time;

    if (!m_maximum_time_since_message.isNull() &&
        time_since_message > m_maximum_time_since_message) {
        LOG_ERROR_S << "Control message is too old. Expected a message from at most "
                    << m_maximum_time_since_message << "s ago. Got a message "
                    << time_since_message << "s old";
        m_statistics.too_old++;
        return false;
    }

    if (!updateRawCommand()) {
        return false;
    }
    _raw_command.write(m_raw_cmd_obj);
    return true;
}

base::Time Task::getLastMessageTime() const {
    return m_decoder->getTime();
}

bool Task::handleAskControlMessage()
{
    if (!m_decoder->validateAskControlMessage()) {
        return false;
    }

    if (!updateRawCommand()) {
        return false;
    }

    return true;
}

bool Task::getIdFromMessage(std::string& out_str)
{
    if (!m_decoder->validateId()) {
        return false;
    }
    out_str = m_decoder->getId();
    return true;
}

// Fill the Raw Command with the JSON data at decoder.
bool Task::updateRawCommand()
{
    try {
        for (uint i = 0; i < axis.size(); ++i) {
            m_raw_cmd_obj.axisValue.at(i) = m_decoder->getValue(axis.at(i));
        }
        for (uint i = 0; i < button.size(); ++i) {
            m_raw_cmd_obj.buttonValue.at(i) =
                m_decoder->getValue(button.at(i)) > button.at(i).threshold;
        }
        m_raw_cmd_obj.time = m_decoder->getTime();
        return true;
    }
    // A failure here means that the client sent a bad message or the
    // mapping isn't good. Just inform the client: return false.
    catch (const std::exception& e) {
        LOG_ERROR_S << "Invalid message, got error: " << e.what() << std::endl;
        return false;
    }
}

Task::Task(std::string const& name, TaskCore::TaskState initial_state)
    : TaskBase(name, initial_state)
{
}

Task::~Task()
{
}

bool Task::configureHook()
{
    if (!TaskBase::configureHook()) {
        return false;
    }

    button = _button_map.get();
    axis = _axis_map.get();

    m_raw_cmd_obj.buttonValue.resize(button.size(), 0);
    m_raw_cmd_obj.axisValue.resize(axis.size(), 0.0);

    m_decoder = std::make_unique<MessageDecoder>();
    m_maximum_time_since_message = _maximum_time_since_message.get();

    return true;
}
bool Task::startHook()
{
    if (!TaskBase::startHook()) {
        return false;
    }

    int port = _port.get();
    m_statistics = Statistics();
    m_server = nullptr;

    mutex server_thread_ready_mutex;
    unique_lock<mutex> server_thread_ready_lock(server_thread_ready_mutex);
    condition_variable server_thread_ready_signal;

    m_server_thread = std::async(std::launch::async, [this, &server_thread_ready_signal, port] {
        auto logger = std::make_shared<PrintfLogger>(Logger::Level::Debug);
        Server server(logger);

        // Needed to terminate in stopHook
        this->m_server = &server;
        server_thread_ready_signal.notify_all();

        auto handler = std::make_shared<JoystickHandler>();
        handler->task = this;
        server.addWebSocketHandler("/ws", handler, true);
        server.serve("", port);
    });

    while (!this->m_server) {
        server_thread_ready_signal.wait(server_thread_ready_lock);
    }

    return true;
}
void Task::updateHook()
{
    auto status = m_server_thread.wait_for(0ms);

    if (status == std::future_status::ready) {
        LOG_ERROR_S << "Server thread unexpectedly terminated" << std::endl;
        exception();
    }

    TaskBase::updateHook();
}
void Task::errorHook()
{
    TaskBase::errorHook();
}
void Task::stopHook()
{
    TaskBase::stopHook();
    m_server->terminate();
    m_server_thread.wait();
}
void Task::cleanupHook()
{
    TaskBase::cleanupHook();
}

void Task::outputStatistics()
{
    m_statistics.time = base::Time::now();
    _statistics.write(m_statistics);
}
