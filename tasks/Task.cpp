/* Generated from orogen/lib/orogen/templates/tasks/Task.cpp */

#include "Task.hpp"

#include <base-logging/Logging.hpp>

#include <seasocks/PrintfLogger.h>
#include <seasocks/Server.h>
#include <seasocks/WebSocket.h>

#include <string.h>
#include <iostream>
#include <json/json.h>

#include <list>

using namespace controldev_websocket;
using namespace controldev;
using namespace seasocks;

struct controldev_websocket::Client {
    WebSocket *connection = nullptr;
    std::string id = "";
    Json::FastWriter fast;

    bool isEmpty(){
        return connection == nullptr;
    }


};
struct controldev_websocket::JoystickHandler : WebSocket::Handler {
    Client pending;
    Client controlling;
    Task *task = nullptr;
    Json::Value msg;
    Json::FastWriter fast;
    Statistics statistic;

    void handleNewConnection(Client newer, Client other){
        Json::Value feedback;
        if (other.isEmpty()){
            feedback["connection_state"]["state"] = "new";
        } else {
            feedback["connection_state"]["state"] = "lost";
            feedback["connection_state"]["peer"] = newer.id.empty() ? "pending connection"
                                                                      : newer.id;
            other.connection->send(fast.write(feedback));
            other.connection->close();
            feedback["connection_state"]["state"] = "stolen";
            feedback["connection_state"]["peer"] = other.id.empty() ? "pending connection"
                                                                      : other.id;
        }
        newer.connection->send(fast.write(feedback));
    };

    void onConnect(WebSocket *socket) override{
        Client new_pending;
        new_pending.connection = socket;
        handleNewConnection(new_pending, pending);
        pending = new_pending;
    }
    void onData(WebSocket *socket, const char *data) override{

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
                LOG_WARN_S << "Control given to the id " << controlling.id << std::endl;
                return;
            }
            LOG_WARN_S << "Handshake with id " << pending.id << " failed" << std::endl;
            msg["connection_state"]["state"] = "handshake failed";
            socket->send(fast.write(msg));
            return;
        }
        result = result && task->handleControlMessage();
        msg["result"] = result;
        ++task->received;

        // Increment errors count if the result is false, do nothing otherwise
        task->errors += result ? 0 : 1;
        statistic.received = task->received;
        statistic.errors = task->errors;
        statistic.time = base::Time::now();
        task->_statistics.write(statistic);
        socket->send(fast.write(msg));
    }
    void onDisconnect(WebSocket *socket) override {
        if (controlling.connection == socket) {
            controlling = Client();
        } else if (pending.connection == socket) {
            pending = Client();
        } else {
            LOG_WARN_S << "Received message from inactive connection" << std::endl;
        }
    }
};

struct controldev_websocket::MessageDecoder{
    Json::Value jdata;
    Json::CharReaderBuilder rbuilder;
    std::unique_ptr<Json::CharReader> const reader;

    MessageDecoder() : reader(rbuilder.newCharReader()) {

    }

    static std::string mapFieldName(Mapping::Type type){
        if (type == Mapping::Type::Button){
            return "buttons";
        }
        if (type == Mapping::Type::Axis){
            return "axes";
        }
        throw "Failed to get Field Name. The type set is invalid";
    }

    bool parseJSONMessage(char const* data, std::string &errors){
        return reader->parse(data, data+std::strlen(data), &jdata, &errors);
    }

    bool validateControlMessage(){
        if (!jdata.isMember(mapFieldName(Mapping::Type::Axis)) ||
            !jdata.isMember(mapFieldName(Mapping::Type::Button)) ||
            !jdata.isMember("time")) {
            LOG_ERROR_S << "Invalid message, it doesn't contain the required fields.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    bool validateAskControlMessage(){
        if (!jdata.isMember("test_message")) {
            LOG_ERROR_S << "Invalid test msg, it doesn't contain the test_message field.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        jdata = jdata["test_message"];
        return validateControlMessage();
    }

    bool validateId(){
        if (!jdata.isMember("id")) {
            LOG_ERROR_S << "Invalid test msg, it doesn't contain the id field.";
            LOG_ERROR_S << std::endl;
            return false;
        }
        return true;
    }

    double getValue(const Mapping &mapping){
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

    std::string getId(){
        auto const& field = jdata["id"];
        return field.asString();
    }

    base::Time getTime(){
        return base::Time::fromMilliseconds(
            jdata["time"].asUInt64()
        );
    }
};

bool Task::parseIncomingWebsocketMessage(char const* data, WebSocket *connection) {
    std::string errs;
    if (!decoder->parseJSONMessage(data, errs)) {
        LOG_ERROR_S << "Failed parsing the message, got error: " << errs << std::endl;
        return false;
    }
    return true;
}

bool Task::handleControlMessage() {
    if (!decoder->validateControlMessage()){
        return false;
    }

    if (!updateRawCommand()){
        return false;
    }
    _raw_command.write(raw_cmd_obj);
    return true;
}

bool Task::handleAskControlMessage() {
    if (!decoder->validateAskControlMessage()){
        return false;
    }

    if (!updateRawCommand()){
        return false;
    }

    return true;
}

bool Task::getIdFromMessage(std::string &out_str) {
    if (!decoder->validateId()){
        return false;
    }
    out_str = decoder->getId();
    return true;
}

// Fill the Raw Command with the JSON data at decoder.
bool Task::updateRawCommand(){
    try {
        for (uint i = 0; i < axis.size(); ++i) {
            raw_cmd_obj.axisValue.at(i) = decoder->getValue(axis.at(i));
        }
        for (uint i = 0; i < button.size(); ++i){
            raw_cmd_obj.buttonValue.at(i) =
                decoder->getValue(button.at(i)) > button.at(i).threshold;
        }
        raw_cmd_obj.time = decoder->getTime();
        return true;
    }
    // A failure here means that the client sent a bad message or the
    // mapping isn't good. Just inform the client: return false.
    catch (const std::exception &e) {
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
    if (! TaskBase::configureHook()){
        return false;
    }

    received = 0;
    errors = 0;

    button = _button_map.get();
    axis = _axis_map.get();

    raw_cmd_obj.buttonValue.resize(button.size(), 0);
    raw_cmd_obj.axisValue.resize(axis.size(), 0.0);

    decoder = new MessageDecoder();
    auto logger = std::make_shared<PrintfLogger>(Logger::Level::Debug);
    server = new Server (logger);

    auto handler = std::make_shared<JoystickHandler>();
    handler->task = this;
    server->addWebSocketHandler("/ws", handler, true);

    return true;
}
bool Task::startHook()
{
    if (! TaskBase::startHook()){
        return false;
    }

    if (!server->startListening(_port.get())) {
        return false;
    }

    thread = new std::thread([this]{ this->server->loop(); });

    return true;
}
void Task::updateHook()
{
    TaskBase::updateHook();
}
void Task::errorHook()
{
    TaskBase::errorHook();
}
void Task::stopHook()
{
    TaskBase::stopHook();
    server->terminate();
    thread->join();
    delete thread;
}
void Task::cleanupHook()
{
    TaskBase::cleanupHook();
    delete server;
    delete decoder;
}
