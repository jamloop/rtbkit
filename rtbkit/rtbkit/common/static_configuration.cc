/* static_configuration.cc
   Mathieu Stefani, 16 décembre 2015
   Copyright (c) 2015 Datacratic.  All rights reserved.
   
   Implementation of the static discovery
*/

#include "static_configuration.h"
#include "jml/arch/exception.h"
#include "jml/utils/file_functions.h"

using Datacratic::PortRange;

namespace RTBKIT {
namespace Discovery {

namespace {
    Json::Value loadJsonFromFile(const std::string & filename)
    {
        ML::File_Read_Buffer buf(filename);
        return Json::parse(std::string(buf.start(), buf.end()));
    }

    Json::Value jsonMember(const Json::Value& value, const char* fieldName, const char* object) {
        if (!value.isMember(fieldName))
            throw ML::Exception("Expected field '%s' in '%s'", fieldName, object);

        return value[fieldName];
    }

    template<typename T>
    struct TypedJson;

    /* This macro helps us generating code for extracting a typed integer from a json value
     * It's used so that we do not have to specialize the template by hand for every common
     * integer type
     */
#define TYPED_JSON_INTEGER(Integer)                                                                              \
    template<>                                                                                                   \
    struct TypedJson<Integer> {                                                                                  \
        static Integer extract(const Json::Value& val, const char* name) {                                       \
            auto v = val.asInt();                                                                                \
            const auto max = std::numeric_limits<Integer>::max();                                                \
            if (v > max)                                                                                         \
                throw ML::Exception("json int value '%s' overflows type '%s' (%llu > %llu)",                     \
                     name, #Integer, static_cast<unsigned long long>(v), static_cast<unsigned long long>(max));  \
            return v;                                                                                            \
        }                                                                                                        \
    };

#define TYPED_JSON_UNSIGNED_INTEGER(Integer)                                                                     \
    template<>                                                                                                   \
    struct TypedJson<Integer> {                                                                                  \
        static Integer extract(const Json::Value& val, const char *name) {                                       \
            auto v = val.asUInt();                                                                               \
            const auto max = std::numeric_limits<Integer>::max();                                                \
            if (v > max)                                                                                         \
                throw ML::Exception("json int value '%s' overflows type '%s' (%llu > %llu)",                     \
                     name, #Integer, static_cast<unsigned long long>(v), static_cast<unsigned long long>(max));  \
            return v;                                                                                            \
        }                                                                                                        \
    };

    TYPED_JSON_INTEGER(int8_t)
    TYPED_JSON_INTEGER(int16_t)
    TYPED_JSON_INTEGER(int32_t)
    TYPED_JSON_INTEGER(int64_t)

    TYPED_JSON_UNSIGNED_INTEGER(uint8_t)
    TYPED_JSON_UNSIGNED_INTEGER(uint16_t)
    TYPED_JSON_UNSIGNED_INTEGER(uint32_t)
    TYPED_JSON_UNSIGNED_INTEGER(uint64_t)

#undef TYPED_JSON_INTEGER
#undef TYPED_JSON_UNSIGNED_INTEGER

    template<>
    struct TypedJson<float> {
        static double extract(const Json::Value& val, const char*) {
            return val.asDouble();
        }
    };

    template<>
    struct TypedJson<double> {
        static double extract(const Json::Value& val, const char*) {
            return val.asDouble();
        }
    };

    template<>
    struct TypedJson<std::string> {
        static std::string extract(const Json::Value& val, const char*) {
            return val.asString();
        }
    };

    template<>
    struct TypedJson<Protocol> {
        static Protocol extract(const Json::Value& val, const char*) {
            auto str = val.asString();

            if (str == "zmq")
                return Protocol::Zmq;
            else if (str == "http")
                return Protocol::Http;
            else if (str == "rest")
                return Protocol::Rest;
            else
                throw ML::Exception("Unknown protocol '%s'", str.c_str());
        }
    };

    template<>
    struct TypedJson<ZmqEndpointType> {
        static ZmqEndpointType extract(const Json::Value& val, const char*) {
            auto str = val.asString();
            if (str == "bus")
                return ZmqEndpointType::Bus;
            else if (str == "publisher")
                return ZmqEndpointType::Publisher;

            throw ML::Exception("Unknown endopint type '%s'", str.c_str());
        }
    };

    template<typename T>
    T typedJsonMember(const Json::Value& value, const char* fieldName, const char* object) {
        auto val = jsonMember(value, fieldName, object);

        return TypedJson<T>::extract(val, fieldName);
    }

    template<typename T>
    T typedJsonValue(const Json::Value& value, const char* name) {
        return TypedJson<T>::extract(value, name);
    }

    void
    jsonForeach(const Json::Value& val, std::function<void (std::string, const Json::Value&)> func) {
        for (auto it = val.begin(), end = val.end(); it != end; ++it) {
            func(it.memberName(), *it);
        }
    }

    Endpoint parseEndpoint(const Json::Value& value, std::string name) {
        const char *n = name.c_str();

        std::string serviceName;
        if (value.isMember("serviceName"))
            serviceName = typedJsonMember<std::string>(value, "serviceName", n);
        else
            serviceName = name;

        std::string alias;

        if (value.isMember("alias"))
            alias = typedJsonMember<std::string>(value, "alias", n);
        else
            alias = name;

        auto protocol    = typedJsonMember<Protocol>(value, "protocol", n);
        Port port;
        if (protocol == Protocol::Rest) {
            port = Port::parseMulti(jsonMember(value, "ports", n), n);
        } else {
            port = Port::parseSingle(jsonMember(value, "port", n), n);
        }

        Endpoint endpoint(
                std::move(name), std::move(alias), std::move(serviceName),
                protocol, std::move(port));

        if (protocol == Protocol::Zmq) {
            endpoint.setData(std::make_shared<ZmqData>(
                    typedJsonMember<ZmqEndpointType>(value, "type", n)));
        }

        return endpoint;

    }
}

Port::operator uint16_t() const {
    if (type != Type::Single)
        throw ML::Exception("Can not retrieve a single value of a multiple ports");

    return value;
}

Port
Port::single(uint16_t value) {
    return Port(value);
}

Port
Port::multiple(const std::map<std::string, uint16_t>& values) {
    return Port(values);
}

Port
Port::parseSingle(const Json::Value& value, const char* name) {
    Port port;
    port.value = typedJsonValue<uint16_t>(value, name);
    port.type = Type::Single;

    return port;
}

Port
Port::parseMulti(const Json::Value& value, const char* name) {
   Port port;

   for (auto it = value.begin(), end = value.end(); it != end; ++it) {
       auto val = typedJsonValue<uint16_t>(*it, name);
       port.values.insert(std::make_pair(it.memberName(), val));
   }

   port.type = Type::Multi;

   return port; 
}

Port::iterator
Port::begin() {
    assertMulti();
    return values.begin();
}

Port::iterator
Port::end() {
    assertMulti();
    return values.end();
}

Port::const_iterator
Port::begin() const {
    assertMulti();
    return values.begin();
}

Port::const_iterator
Port::end() const {
    assertMulti();
    return values.end();
}

Port::const_iterator
Port::find(const std::string& name) const {
    assertMulti();
    return values.find(name);
}

void
Port::assertMulti() const {
    if (type != Type::Multi)
        throw ML::Exception("Invalid operation on single port");
}

Port
operator+(Port lhs, uint16_t value)
{
    if (lhs.isSingle()) {
        lhs.value += value;
    } else {
        for (auto& val: lhs.values) {
            val.second += value;
        }
    }

    return lhs;
}

Binding::Context
Binding::context(const Endpoints& endpoints, std::string name) {
    return Binding::Context { endpoints, std::move(name) };
}

Binding
Binding::fromExpression(const Json::Value& value, const Context& context) {
    return Binding::fromExpression(value.asString(), context);
}

Binding
Binding::fromExpression(const std::string& value, const Context& context) {

    auto findEndpoint = [&](const std::string& name) {
        auto it = context.endpoints.find(name);
        if (it == std::end(context.endpoints))
            throw ML::Exception("Could not find endpoint '%s' for binding expression '%s'", name.c_str(), context.name.c_str());

        return it->second;
    };

    auto pos = value.find(':');
    if (pos == std::string::npos) {
        auto ep = findEndpoint(value);
        return Binding(ep, ep.port());
    } else {
        std::string e = value.substr(0, pos);
        std::string p = value.substr(pos + 1);

        auto ep = findEndpoint(e);

        const char *raw = p.c_str();
        if (*raw == '$') {
            if (*++raw != '+')
                throw ML::Exception("Binding expression for '%s': expected '+' got '%c' (%d)",
                    context.name.c_str(), *raw, static_cast<int>(*raw));

            uint16_t incr = std::strtol(++raw, nullptr, 10);
            if (incr == 0)
                throw ML::Exception("Invalid operation for binding expression '%s': '%s'", context.name.c_str(), p.c_str());

            Port port = ep.port() + incr;
            return Binding(ep, ep.port() + incr);
        }

        return Binding(ep, ep.port());
    }
}

bool
Service::Node::hasBinding(const std::string& name) const {
    return hasBindingImpl(name).first;
}

Binding
Service::Node::binding(const std::string& name) const {
    auto binding = hasBindingImpl(name);
    if (!binding.first)
        throw ML::Exception("Could not find binding '%s' for node '%s'",
                name.c_str(), serviceName.c_str());

    return *binding.second;
}

std::vector<Binding>
Service::Node::protocolBindings(Protocol protocol) const {
    std::vector<Binding> res;
    for (const auto& bind: bindings) {
        auto ep = bind.endpoint();
        if (ep.protocol() == protocol)
            res.push_back(bind);
    }

    return res;
}

std::pair<bool, std::vector<Binding>::const_iterator>
Service::Node::hasBindingImpl(const std::string& name) const {
    auto it = std::find_if(std::begin(bindings), std::end(bindings), [&](const Binding& binding) {
        auto ep = binding.endpoint();
        return ep.serviceName() == name || ep.name() == name || ep.alias() == name;
    });

    return std::make_pair(it != std::end(bindings), it);
}

void
Service::addNode(const Service::Node& node) {
    nodes.insert(std::make_pair(node.serviceName, node));
}

bool
Service::hasNode(const std::string& name) const {
    return hasNodeImpl(name).first;
}

Service::Node
Service::node(const std::string& name) const {
    auto node = hasNodeImpl(name);
    if (!node.first)
        throw ML::Exception("Node '%s' for service '%s' does not exist",
                name.c_str(), className.c_str());

    return node.second->second;
}

std::vector<Service::Node>
Service::allNodes() const {
    std::vector<Service::Node> res;
    for (const auto& node: nodes) {
        res.push_back(node.second);
    }

    return res;
}

std::pair<bool, std::map<std::string, Service::Node>::const_iterator>
Service::hasNodeImpl(const std::string& name) const {
    auto it = nodes.find(name);
    return std::make_pair(it != std::end(nodes), it);
}

StaticDiscovery
StaticDiscovery::fromFile(const std::string& fileName)
{
    return StaticDiscovery::fromJson(loadJsonFromFile(fileName));
}

StaticDiscovery
StaticDiscovery::fromJson(const Json::Value& value) {
    StaticDiscovery res;
    res.parseFromJson(value);
    return res;
}

void
StaticDiscovery::parseFromFile(const std::string& fileName) {
    return parseFromJson(loadJsonFromFile(fileName));
}

void
StaticDiscovery::parseFromJson(const Json::Value& value) {

    if (!value.isObject())
        throw ML::Exception("root: expected a json object");

    auto &epts = value["endpoints"];
    if (!epts.isObject())
        throw ML::Exception("endpoints: expected a json object");

    std::map<std::string, Endpoint> endpoints;

    jsonForeach(epts, [&](std::string name, const Json::Value& value) {
        auto endpoint = parseEndpoint(value, name.c_str());
        endpoints.insert(std::make_pair(std::move(name), std::move(endpoint)));
    });

    auto &srvs = value["services"];
    if (!srvs.isObject())
        throw ML::Exception("services: expected a json object");

    std::map<std::string, Service> services;
    jsonForeach(srvs, [&](std::string srvClass, const Json::Value& serviceConfig) {
        Service service(srvClass);

        jsonForeach(serviceConfig, [&](std::string serviceName, const Json::Value& value) {
            auto hostName = typedJsonMember<std::string>(value, "hostname", serviceName.c_str());
            auto bindArr = jsonMember(value, "bind", serviceName.c_str());
            if (!bindArr.isArray())
                throw ML::Exception("bind for '%s': expected array", serviceName.c_str());

            auto shardIndex = value.get("shard", -1).asInt();

            std::vector<Binding> bindings;
            for (const auto& bind: bindArr) {
                auto binding = Binding::fromExpression(bind, Binding::context(endpoints, serviceName));
                bindings.push_back(std::move(binding));
            }

            service.addNode(Service::Node(std::move(serviceName), std::move(hostName), std::move(bindings), shardIndex));

        });

        services.insert(std::make_pair(std::move(srvClass), std::move(service)));
    });

    this->endpoints = std::move(endpoints);
    this->services = std::move(services);
}

void
StaticConfigurationService::init(const std::shared_ptr<StaticDiscovery>& discovery) {
    this->discovery = discovery;
}

Json::Value
StaticConfigurationService::getJson(
        const std::string& value,
        Datacratic::ConfigurationService::Watch watch)
{
    auto keyParts = splitKey(value);
    Json::Value res;

    if (keyParts[0] == "serviceClass") {
        ExcAssertEqual(keyParts.size(), 3);

        auto serviceClass = keyParts[1];
        auto service = discovery->service(serviceClass);
        auto node = service.node(keyParts[2]);

        res["serviceLocation"] = currentLocation;
        res["serviceName"] = node.serviceName;
        res["servicePath"] = node.serviceName;
        res["shardIndex"] = node.shardIndex;
    }
    else {
        auto ep = keyParts[1];
        if (ep == "zeromq" || ep == "http") {
            auto node = discovery->node(keyParts[0]);

            auto bindings = node.protocolBindings(Protocol::Rest);
            ExcAssertEqual(bindings.size(), 1);

            auto bind = bindings[0];
            auto port = bind.port();
            ExcAssert(port.isMulti());

            auto portValue = port.find(ep);
            ExcAssert(portValue != port.end());

            auto p = portValue->second;

            auto uri = "tcp://" + node.hostName + ":" + std::to_string(p);

            Json::Value info;
            auto& transports = info["transports"];
            transports[0]["name"] = "tcp";
            //transports[0]["addr"] = addr;
            transports[0]["hostScope"] = "*";
            transports[0]["port"] = p;

            transports[1]["name"] = "zeromq";
            // @Todo: hard-coded for now
            transports[1]["socketType"] = 6;
            transports[1]["uri"] = uri;
            info["zmqConnectUri"] = uri;
            res.append(info);

        }
        else {
            auto nodes = discovery->nodes(keyParts[0]);
            for (const auto& node: nodes) {

                if (!node.hasBinding(ep)) continue;

                auto binding = node.binding(ep);
                auto port = binding.port();
                ExcAssert(port.isSingle());

                auto p = static_cast<uint16_t>(port);

                auto uri = "tcp://" + node.hostName + ":" + std::to_string(p);

                Json::Value info;
                auto& transports = info["transports"];
                transports[0]["name"] = "tcp";
                //transports[0]["addr"] = addr;
                transports[0]["hostScope"] = "*";
                transports[0]["port"] = p;

                transports[1]["name"] = "zeromq";
                // @Todo: hard-coded for now
                transports[1]["socketType"] = 6;
                transports[1]["uri"] = uri;
                info["zmqConnectUri"] = uri;
                res.append(info);
            }
        }

    }


    return res;
}

void
StaticConfigurationService::set(
        const std::string& key,
        const Json::Value& value)
{
}

std::string
StaticConfigurationService::setUnique(const std::string& key, const Json::Value& value) {
    return "";
}

std::vector<std::string>
StaticConfigurationService::getChildren(
        const std::string& key,
        Datacratic::ConfigurationService::Watch watch) {
    /* @Note this a big hack that has been done by "reversing" what is done
     * by the ZooKeeperConfigurationService class
     */
    std::vector<std::string> res;

    auto keyParts = splitKey(key);
    ExcAssert(!keyParts.empty());

    // @Temporary until I figure out the monitor special hack
    if (keyParts.size() > 2) return res;

    //ExcAssertEqual(keyParts.size(), 2);

    /* For a given serviceClass (e.g rtbRouter), we just have to fetch all the nodes
     * under that class
     */
    if (keyParts[0] == "serviceClass") {

        auto serviceClass = keyParts[1];
        auto service = discovery->service(serviceClass);
        auto nodes = service.allNodes();
        for (const auto& node: nodes) {
            res.push_back(node.serviceName);
        }
    }
    /* Otherwise, assume that the node has a tcp/ child. I figured it out by dumping keys that where
     * written inside ZooKeeper by the ZookeeperConfigurationService class
     */
    else {
        res.push_back("tcp");
    }

    return res;
}

bool
StaticConfigurationService::forEachEntry(
        const Datacratic::ConfigurationService::OnEntry& onEntry,
        const std::string& startPrefix) const {
    return false;
}

void
StaticConfigurationService::removePath(const std::string& path) {
}

std::vector<std::string>
StaticConfigurationService::splitKey(const std::string& key) const {
    std::vector<std::string> res;

    std::istringstream iss(key);
    std::string part;
    while (std::getline(iss, part, '/'))
        res.push_back(std::move(part));

    return res;
}

StaticPortRangeService::StaticPortRangeService(
        const std::shared_ptr<StaticDiscovery>& discovery,
        const std::string& nodeName) 
    : discovery(discovery)
    , nodeName(nodeName) {
}

PortRange
StaticPortRangeService::getRange(const std::string& name) {
    auto nodes = discovery->nodes(nodeName);

    if (nodes.empty())
        throw ML::Exception("Did not find any node for '%s'", name.c_str());

    /* First try to find a "classic" binding, i.e every except Rest (zmq and http) */
    for (const auto& node: nodes) {
        if (!node.hasBinding(name)) continue;

        auto binding = node.binding(name);
        auto port = binding.port();
        ExcAssert(port.isSingle());

        auto p = static_cast<uint16_t>(port);
        return PortRange(p);
    }

    auto parts = splitPort(name);
    auto back = parts.back();

    /* If nothing was found, then check for any Rest binding */
    if (back == "zmq" || back == "http") {
        for (const auto& node: nodes) {
            auto bindings = node.protocolBindings(Protocol::Rest);
            if (bindings.empty()) continue;

            ExcAssertEqual(bindings.size(), 1);

            auto bind = bindings[0];
            auto port = bind.port();
            ExcAssert(port.isMulti());

            /* Our standard name for a port that is bound to a zeromq service is "zeromq". However,
             * classic bootstrap uses <serviceName>.zmq as a name that's why we're normalizing
             * "zmq" to "zeromq" underneath
             */
            auto it = port.find(back == "zmq" ? "zeromq" : back);

            if (it == std::end(port))
                throw ML::Exception("Could not find PortRange for '%s'", name.c_str());

            return PortRange(it->second);
        }
    }

    throw ML::Exception("Could not find PortRange for '%s'", name.c_str());
}

void
StaticPortRangeService::dump(std::ostream& os) const {
}

std::vector<std::string>
StaticPortRangeService::splitPort(const std::string& name) const {
    std::vector<std::string> res;

    std::istringstream iss(name);
    std::string part;
    while (std::getline(iss, part, '.'))
        res.push_back(std::move(part));

    return res;
}

} // namespace Discovery

} // namespace RTBKIT
