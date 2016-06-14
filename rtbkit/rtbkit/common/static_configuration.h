/* static_configuration.h
   Mathieu Stefani, 16 décembre 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Static configuration system for RTBKit
*/

#pragma once

#include <map>
#include <string>
#include <memory>
#include <iostream>
#include "soa/jsoncpp/json.h"
#include "soa/service/zmq_endpoint.h"
#include "soa/service/zmq_named_pub_sub.h"
#include "soa/service/rest_service_endpoint.h"
#include "soa/service/service_base.h"
#include "soa/service/port_range_service.h"
#include "jml/arch/exception.h"

namespace RTBKIT {
namespace Discovery {

    enum class Protocol { Zmq, Http, Rest };

    /* This structure represents a given port or group of ports.
     * For example, a REST service requires two ports: one for
     * zmq and one for http. In this case, this structure will hold
     * a list of two ports, for zmq and http
     *
     * It's a poor representation of a "tagged union" like so:
     *    enum Port {
     *        Single(u16),
     *        Multiple(Map<String, u16>)
     *   }
     */
    struct Port {
        typedef std::map<std::string, uint16_t> Values;
        typedef Values::iterator iterator;
        typedef Values::const_iterator const_iterator;

        Port()
            : type(Null)
        { }

        friend Port operator+(Port lhs, uint16_t value);

        operator uint16_t() const;

        static Port single(uint16_t value);
        static Port multiple(const Values& values);

        static Port parseSingle(const Json::Value& value, const char* name);
        static Port parseMulti(const Json::Value& value, const char* name);

        iterator begin();
        iterator end();

        const_iterator begin() const;
        const_iterator end() const;

        const_iterator find(const std::string& name) const;

        bool isNull() const { return type == Type::Null; }
        bool isSingle() const { return type == Type::Single; }
        bool isMulti() const { return type == Type::Multi; }

    private:
        Port(uint16_t value)
            : value(value)
            , type(Type::Single)
        { }
        Port(const Values& values)
            : values(values)
            , type(Type::Multi)
        { }

        void assertMulti() const;

        enum Type { Null, Single, Multi };
        uint16_t value;
        Values values;

        Type type;
    };

    Port operator+(Port lhs, uint16_t value);

    /* An endpoint is the server-side part of a communication layer. For example, agents
     * are talking to routers through a particular zeromq channel. The component in the router
     * that accepts connections from the agents is called an endpoint.
     *
     * An endpoint has at least a name and a protocol which can be either zmq, http or rest for
     * both http and zmq
     */
    struct Endpoint {
        Endpoint(std::string name, std::string serviceName, Protocol protocol, Port port)
            : name_(std::move(name))
            , serviceName_(std::move(serviceName))
            , alias_(name_)
            , protocol_(protocol)
            , port_(std::move(port))
            , extraData_(nullptr)
        { }

        Endpoint(std::string name, std::string alias, std::string serviceName, Protocol protocol, Port port)
            : name_(std::move(name))
            , serviceName_(std::move(serviceName))
            , alias_(std::move(alias))
            , protocol_(protocol)
            , port_(std::move(port))
            , extraData_(nullptr)
        { }

        std::string name() const { return name_; }
        std::string serviceName() const { return serviceName_; }
        std::string alias() const { return alias_; }
        Protocol protocol() const { return protocol_; }
        Port port() const { return port_; }

        template<typename Ptr>
        void setData(Ptr&& data) {
            extraData_ = std::forward<Ptr>(data);
        }

        template<typename T>
        T *data() const {
            return std::static_pointer_cast<T>(extraData_).get();
        }

    private:
        std::string name_;
        std::string serviceName_;
        std::string alias_;
        Protocol protocol_;
        Port port_;
        std::shared_ptr<void> extraData_;
    };

    enum class ZmqEndpointType { Bus, Publisher };

    struct ZmqData {
        ZmqData(ZmqEndpointType type)
            : type(type)
        { }

        ZmqEndpointType type;
    };

    typedef std::map<std::string, Endpoint> Endpoints;

    /* A binding represents a "physical" bind between an endpoint and a given port.
     *
     * To be able to accept incoming connections, an endpoint must be bound to a specific
     * port and assigned a local address by the kernel. This class is used to keep track of
     * which ports are being assigned to which endpoints
     */
    struct Binding {
        struct Context {
            Endpoints endpoints;
            std::string name;
        };

        Binding(Endpoint endpoint, Port port)
            : endpoint_(std::move(endpoint))
            , port_(std::move(port))
        { }

        static Context context(const Endpoints& endpoint, std::string name);

        static Binding fromExpression(const Json::Value& value, const Context& context);
        static Binding fromExpression(const std::string& value, const Context& context);

        Endpoint endpoint() const { return endpoint_; }
        Port port() const { return port_; }

    private:
        Endpoint endpoint_;
        Port port_;
    };

    /*
     * A service is a components that exposes one or multiple endpoints. For example, the router
     * is a service that exposes a logger endpoint as well as a zmq endpoint for agents.
     */
    struct Service {
        struct Node {
            Node(std::string serviceName, std::string hostName, const std::vector<Binding>& bindings)
                : serviceName(std::move(serviceName))
                , hostName(std::move(hostName))
                , bindings(bindings)
            { }

            bool hasBinding(const std::string& name) const;
            Binding binding(const std::string& name) const;
            std::vector<Binding> protocolBindings(Protocol protocol) const;
            std::string fullServiceName(const std::string& endpointName) const {
                return serviceName + "/" + endpointName;
            }

            std::string serviceName;
            std::string hostName;

        private:
            std::vector<Binding> bindings;
            std::pair<bool, std::vector<Binding>::const_iterator>
            hasBindingImpl(const std::string& name) const;
        };

        Service(std::string className)
            : className(std::move(className))
        { }

        void addNode(const Node& node);
        bool hasNode(const std::string& name) const;
        Node node(const std::string& name) const;
        std::vector<Node> allNodes() const;

    private:
        std::pair<bool, std::map<std::string, Node>::const_iterator>
        hasNodeImpl(const std::string& name) const;
        std::map<std::string, Node> nodes;
        std::string className;
    };

    class StaticDiscovery {
    public:
         static StaticDiscovery fromFile(const std::string& fileName);
         static StaticDiscovery fromJson(const Json::Value& value);

         void parseFromFile(const std::string& fileName);
         void parseFromJson(const Json::Value& value);

         Service::Node node(const std::string& serviceName) const {
             for (const auto& service: services) {
                 if (service.second.hasNode(serviceName))
                     return service.second.node(serviceName);
             }

             throw ML::Exception("Unknown node '%s'", serviceName.c_str());
         }

         std::vector<Service::Node>
         nodes(const std::string& serviceName) const {

             std::vector<Service::Node> nodes;

             for (const auto& service: services) {
                 if (service.second.hasNode(serviceName))
                    nodes.push_back(service.second.node(serviceName));
             }
             return nodes;
         }

         Service service(const std::string& serviceClass) const {
             auto it = services.find(serviceClass);
             if (it == std::end(services))
                 throw ML::Exception("Unknown service '%s'", serviceClass.c_str());

             return it->second;
         }

    private:
         Endpoints endpoints;
         std::map<std::string, Service> services;
    };

    struct StaticConfigurationService : public Datacratic::ConfigurationService {

        void init(const std::shared_ptr<StaticDiscovery>& discovery);

        Json::Value getJson(
                const std::string& value, Watch watch = Watch());

        void set(const std::string& key,
                const Json::Value& value);

        std::string setUnique(const std::string& key, const Json::Value& value);

        std::vector<std::string>
        getChildren(const std::string& key,
                    Watch watch = Watch());

        bool forEachEntry(const OnEntry& onEntry,
                          const std::string& startPrefix = "") const;

        void removePath(const std::string& path);

    private:
        std::vector<std::string> splitKey(const std::string& key) const;
        std::shared_ptr<StaticDiscovery> discovery;

    };

    struct StaticPortRangeService : public Datacratic::PortRangeService {

        StaticPortRangeService(
                const std::shared_ptr<StaticDiscovery>& discovery,
                const std::string& nodeName);

        Datacratic::PortRange getRange(const std::string& name);
        void dump(std::ostream& stream = std::cerr) const;

    private:
        std::shared_ptr<StaticDiscovery> discovery;
        std::string nodeName;

        std::vector<std::string> splitPort(const std::string& name) const;
    };

} // namespace Discovery

} // namespace RTBKIT

namespace std {
}
