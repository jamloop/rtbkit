/* creative_configuration.h                           -*- C++ -*-
   Thomas Sanchez, 18 October 2013
   Copyright (c) 2013 mbr targeting GmbH.  All rights reserved.
*/

#pragma once

#include <initializer_list>
#include <string>
#include <vector>
#include <map>

#include <boost/thread.hpp>

#include "rtbkit/common/exchange_connector.h"
#include "rtbkit/common/auction.h"
#include "rtbkit/common/expand_variable.h"
#include "rtbkit/common/creative_field.h"
#include "rtbkit/common/currency.h"

namespace RTBKIT {

namespace {
    OpenRTB::Geo *getGeo(const BidRequest& br) {
        if (br.user) {
            if (br.user->geo)
                return br.user->geo.get();
        }
        if (br.device) {
            if (br.device->geo)
                return br.device->geo.get();
        }

        return nullptr;
    }
}

template <typename CreativeData>
class CreativeConfiguration
{

public:
    static const std::string VARIABLE_MARKER_BEGIN;
    static const std::string VARIABLE_MARKER_END;

    typedef CreativeField<CreativeData> Field;

    struct Context {
        const Creative& creative;
        const Auction::Response& response;
        const BidRequest& bidrequest;
        int spotNum;
    };

    enum class Verbosity { Verbose, Quiet };

    typedef std::function<std::string &(std::string &)> ExpanderFilterCallable;
    typedef std::map<std::string, ExpanderFilterCallable> ExpanderFilterMap;
    typedef std::function<std::string(const Context &)> ExpanderCallable;
    typedef std::map<std::string, ExpanderCallable> ExpanderMap;

    struct Expander;

    CreativeConfiguration(const std::string& exchange)
    : exchange_(exchange)
    {
        expanderDict_ = {
        {
                "exchange",
                std::bind(&CreativeConfiguration::exchange_, this)
        },

        {
            "creative.id",
            [](const Context& ctx)
            { return std::to_string(ctx.creative.id); }
        },

        {
            "creative.name",
            [](const Context& ctx)
            { return ctx.creative.name; }
        },

        {
            "creative.width",
            [](const Context& ctx)
            { return std::to_string(ctx.creative.format.width); }
        },

        {
            "creative.height",
            [](const Context& ctx)
            { return std::to_string(ctx.creative.format.height); }
        },

        {
            "bidrequest.id",
            [](const Context& ctx)
            { return ctx.bidrequest.auctionId.toString(); }
        },

        {
            "bidrequest.user.id",
            [](const Context& ctx) -> std::string
            {
                if ( ctx.bidrequest.user){
                    return ctx.bidrequest.user->id.toString();
                }
                return "";
            }
        },

        {
            "bidrequest.video.width",
            [](const Context& ctx) -> std::string
            {
		const auto& imp = ctx.bidrequest.imp[ctx.spotNum];
                if (imp.video) {
                    auto w = imp.video->w.value();
                    if (w != -1) { 
		        return std::to_string(w);
                    }
                }
                return "";
            }
        },

        {
            "bidrequest.video.height",
            [](const Context& ctx) -> std::string
            {
		const auto& imp = ctx.bidrequest.imp[ctx.spotNum];
                if (imp.video) {
                    auto h = imp.video->h.value();
                    if (h != -1) { 
		        return std::to_string(h);
                    }
                }
                return "";
            }
        },

        {
            "bidrequest.video.pos",
            [](const Context& ctx) -> std::string
            {
		const auto& imp = ctx.bidrequest.imp[ctx.spotNum];
                if (imp.video) {
                     using OpenRTB::AdPosition;

                     auto pos = imp.video->pos;
                     switch (static_cast<AdPosition::Vals>(pos.value()))
                     {
                     case AdPosition::UNSPECIFIED:
                         return "unspecified";
                     case AdPosition::UNKNOWN:
                         return "unknown";
                     case  AdPosition::ABOVE:
                         return "above";
                     case AdPosition::BETWEEN_DEPRECATED:
                         return "between";
                     case AdPosition::BELOW:
                         return "below";
                     case AdPosition::HEADER:
                         return "header";
                     case AdPosition::FOOTER:
                         return "footer";
                     case AdPosition::SIDEBAR:
                         return "sidebar";
                     case AdPosition::FULLSCREEN:
                         return "fullscreen";
                     }
                }
                return "";
            }
        },

        {
            "bidrequest.publisher.id",
            [](const Context& ctx) -> std::string
            {
                auto const& br = ctx.bidrequest;
                if (br.site && br.site->publisher) {
                    return br.site->publisher->id.toString();
                } else if (br.app && br.app->publisher) {
                    return br.app->publisher->id.toString();
                }
		return "";
            }
        },
    	{
            "bidrequest.site.page",
            [](const Context& ctx) -> std::string { 
                if (ctx.bidrequest.site) {
                    std::string SP_long = ctx.bidrequest.site->page.toString();
                    std::size_t found = SP_long.find("?");

                    if (found!=std::string::npos){
                        return SP_long.substr(0,found+1);
                    }
                    else {
                        return SP_long;
                    }
                } else if (ctx.bidrequest.app) {
                    return std::string("apps://") + ctx.bidrequest.app->name.rawString() + '/';
                }

                return ""; 
            } 
        },
        {
            "bidrequest.device.ip",
            [](const Context& ctx) -> std::string { 
                if (ctx.bidrequest.device && !ctx.bidrequest.device->ip.empty()) {
                    return ctx.bidrequest.device->ip; 
                }
                return "";
            }
        },
        {
            "bidrequest.language",
            [](const Context& ctx) -> std::string { 
                if (!ctx.bidrequest.language.empty()) {
                    return ctx.bidrequest.language.rawString();
                }
                return "";
            }
	},
        {
            "bidrequest.ip",
            [](const Context& ctx) -> std::string { 
                if (!ctx.bidrequest.ipAddress.empty()) {
                    return ctx.bidrequest.ipAddress;
                }
                return "";
            }
        },
        {
            "bidrequest.ua",
            [](const Context& ctx) -> std::string { 
                if (!ctx.bidrequest.userAgent.empty()) {
                    return ctx.bidrequest.userAgent.rawString();
                }
                return "";
            }
        },
        {
            "bidrequest.timestamp",
            [](const Context& ctx)
            { return std::to_string( ctx.bidrequest.timestamp.secondsSinceEpoch() ); }
        },
        {
            "bidrequest.geo.zip",
            [](const Context& ctx) -> std::string {
                auto geo = getGeo(ctx.bidrequest);
                if (geo) {
                    if (!geo->zip.empty())
                        return geo->zip.rawString();
                }

                return "";
            }
        },
        {
            "bidrequest.geo.region",
            [](const Context& ctx) -> std::string {
                auto geo = getGeo(ctx.bidrequest);
                if (geo) {
                    if (!geo->region.empty())
                        return geo->region;
                }
                return "";
            }
        },
        {
            "bidrequest.geo.country",
            [](const Context& ctx) -> std::string {
                auto geo = getGeo(ctx.bidrequest);
                if (geo) {
                    if (!geo->country.empty())
                        return geo->country;
                }
                return "";
            }
        },
        {
            "bidrequest.geo.metro",
            [](const Context& ctx) -> std::string {
                auto* geo = getGeo(ctx.bidrequest);
                if (geo) {
                    if (!geo->metro.empty())
                        return geo->metro;
                }
                return "";
            }
        },
        {
            "bidrequest.geo.city",
            [](const Context& ctx) -> std::string {
                auto* geo = getGeo(ctx.bidrequest);
                if (geo) {
                    if (!geo->city.empty())
                        return geo->city.rawString();
                }
                return "";
            }
        },
        {
            "bidrequest.device.type",
            [](const Context& ctx) -> std::string {
                const auto& br = ctx.bidrequest;
                if (br.device) {
                    auto type = static_cast<OpenRTB::DeviceType::Vals>(br.device->devicetype.val);
                    if (type !=  OpenRTB::DeviceType::Vals::UNSPECIFIED) {
                        return std::to_string(br.device->devicetype.val);
                    }
                }

                return "";
            }
        },
        {
            "bidrequest.app.bundle",
            [](const Context& ctx) -> std::string {
                if (ctx.bidrequest.app)
                    return ctx.bidrequest.app->bundle.rawString();

                return "";
            }
        },
        {
            "bidrequest.app.name",
            [](const Context& ctx) -> std::string {
                if (ctx.bidrequest.app)
                    return ctx.bidrequest.app->name.rawString();
                return "";
            }
        },
        {
            "bidrequest.app.storeurl",
            [](const Context& ctx) -> std::string {
                if (ctx.bidrequest.app)
                    return ctx.bidrequest.app->storeurl.toString();
                return "";
            }
        },
        {
            "bidrequest.video.linearity",
            [](const Context& ctx) -> std::string {
                auto& spot = ctx.bidrequest.imp[ctx.spotNum];
                return std::to_string(spot.video->linearity.val);
            }
        },
        {
            "bidrequest.video.playbackmethod",
            [](const Context& ctx) -> std::string {
                auto& spot = ctx.bidrequest.imp[ctx.spotNum];
                std::ostringstream oss;
                size_t i = 0;
                for (; i < spot.video->playbackmethod.size() - 1; ++i) {
                    oss << spot.video->playbackmethod[i].val << ",";
                }
                oss << spot.video->playbackmethod[i].val;
                return oss.str();
            }
        },
        {
            "bidrequest.video.api",
            [](const Context& ctx) -> std::string {
                auto& spot = ctx.bidrequest.imp[ctx.spotNum];
                std::ostringstream oss;
                size_t i = 0;
                for (; i < spot.video->api.size() - 1; ++i) {
                    oss << spot.video->api[i].val << ",";
                }
                oss << spot.video->api[i].val;
                return oss.str();
            }
        },
        {
            "bidrequest.site.ref",
            [](const Context& ctx) -> std::string {
                if (ctx.bidrequest.site) {
                    return ctx.bidrequest.site->ref.toString();
                }
                return "";
            }
        },

        {
            "response.account",
            [](const Context& ctx)
            { return ctx.response.account.toString(); }
        },
        {
            "imp.id",
            [](const Context& context)
            { return context.bidrequest.imp[context.spotNum].id.toString(); }
        },
        {
            "bid.price",
            [](const Context& context) {
                return to_string(static_cast<double>(USD_CPM(context.response.price.maxPrice)));
            }
        }
        };

        filters_ = {
            {
                "lower",
                [](std::string& value) -> std::string&
                {
                    boost::algorithm::to_lower(value);
                    return value;
                }
            },
            {
                "upper",
                [](std::string& value) -> std::string&
                {
                    boost::algorithm::to_upper(value);
                    return value;
                }
            },
            {
                "urlencode",
                [](std::string& value) -> std::string&
                {
                    std::string result;
                    for (auto c: value) {
                        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                            result += c;
                        else result += ML::format("%%%02X", c);
                    }

                    value = std::move(result);
                    return value;
                }
            },
        };

    }

    Field & addField(const std::string & name,
                     typename Field::Handler handler)
    {
        return fields_[name] = Field(name, handler);
    }

    RTBKIT::ExchangeConnector::ExchangeCompatibility
    handleCreativeCompatibility(const Creative& creative,
                                const bool includeReasons,
                                Verbosity verbosity = Verbosity::Quiet) const;

    std::string expand(const std::string& templateString,
                       const Context& context) const;

    void addExpanderVariable(const std::string& key, ExpanderCallable value)
    {
        expanderDict_[key] = value;
    }

    void addExpanderFilter(const std::string& filter,
                           ExpanderFilterCallable callable)
    {
        filters_[filter] = callable;
    }

private:
    std::vector<ExpandVariable>
    extractVariables(const std::string& snippet) const;

    Expander
    generateExpander(const std::vector<ExpandVariable>& variables) const;

    ExpanderCallable getAssociatedCallable(ExpandVariable const& var) const;
    std::string jsonValueToStr(Json::Value const& val) const;

    ExpanderMap expanderDict_;
    ExpanderFilterMap filters_;

    std::map<std::string, Field> fields_;
    const std::string exchange_;

    /**
     * The map is mutable because it is populated in the
     * getCreativeCompatibility and this member function is required to be
     * const
     */
    mutable std::unordered_map<std::string, Expander> expanders_;
    mutable boost::shared_mutex mutex_;
};

template <typename CreativeData>
const std::string CreativeConfiguration<CreativeData>::VARIABLE_MARKER_BEGIN = "%{";

template <typename CreativeData>
const std::string CreativeConfiguration<CreativeData>::VARIABLE_MARKER_END = "}";

template <typename CreativeData>
RTBKIT::ExchangeConnector::ExchangeCompatibility
CreativeConfiguration<CreativeData>::handleCreativeCompatibility(
    const Creative& creative, const bool includeReasons, Verbosity verbosity) const
{

    RTBKIT::ExchangeConnector::ExchangeCompatibility result;
    result.setCompatible();

    auto const& config = creative.providerConfig[exchange_];

    if (config == Json::Value::null) {
        result.setIncompatible("No configuration for exchange: " + exchange_,
                               includeReasons);
        return result;
    }

    auto data = std::make_shared<CreativeData>();

    for (const auto & pair : fields_) {
        const auto & field = pair.second;
        auto value = field.extractJsonValue(config);

        if (value.isNull()) {
            std::ostringstream oss;
            oss << "test" << ": " << creative.name << " does not have the "
                << (field.isRequired() ? "required" : "optional")
                << " configuration variable '" << field.getName() << "'";

            if (field.isRequired()) {
                result.setIncompatible(oss.str(), includeReasons);
                return result;
            } else if (verbosity == Verbosity::Verbose) {
                std::cerr << oss.str() << std::endl;
            }

        } else {
            try {
                if (!field(value, *data)) {
                    result.setIncompatible(
                        exchange_ + ": " + creative.name + ": value: " +
                            value.toString() +
                            " was not handled properly by the connector.",
                        includeReasons);
                    return result;
                }
            }
            catch (const std::exception & exc)
            {
                result.setIncompatible(exchange_ + ": " + creative.name +
                                           ": value: " + value.toString() +
                                           " was not handled properly by the "
                                           "connector, exception:" +
                                           exc.what(),
                                       includeReasons);
                return result;
            }

            if (field.isSnippet()) {
                // assume string
                auto const& snippet = value.asString();
                auto expander = generateExpander(extractVariables(snippet));
                boost::unique_lock<boost::shared_mutex> lock(mutex_);
                expanders_[snippet] = expander;
            }
        }
    }

    result.info = data;
    return result;
}

template <typename CreativeData>
std::vector<ExpandVariable>
CreativeConfiguration<CreativeData>::extractVariables(
    const std::string& snippet) const
{
    std::vector<ExpandVariable> variables;

    for (auto index = snippet.find(VARIABLE_MARKER_BEGIN), indexEnd = size_t(0);
         index != std::string::npos;
         index = snippet.find(VARIABLE_MARKER_BEGIN, indexEnd)) {

        auto vBegin = index + VARIABLE_MARKER_BEGIN.size();
        indexEnd = snippet.find(VARIABLE_MARKER_END, vBegin);
        if (indexEnd == std::string::npos) {
            throw std::invalid_argument(exchange_ + ": starting at pos " +
                                        std::to_string(vBegin) + ", " +
                                        VARIABLE_MARKER_END + " is expected");
        }

        auto variable = snippet.substr(vBegin, indexEnd - vBegin);
        indexEnd += VARIABLE_MARKER_END.size();
        variables.emplace_back(variable, index, indexEnd);
    }

    return std::move(variables);
}

template <typename CreativeData>
typename CreativeConfiguration<CreativeData>::ExpanderCallable
CreativeConfiguration<CreativeData>::getAssociatedCallable(
    ExpandVariable const& var) const
{
    auto it = expanderDict_.find(var.getVariable());
    if (it != expanderDict_.end()) {
        return it->second;
    }

    auto const& path = var.getPath();
    auto const& section = path[0];

    auto getter = [this, var](Json::Value const & jsonVal,
                              const Context & context)->std::string
    {

        auto const& path = var.getPath();
        auto val = jsonVal;
        for (auto it = std::begin(path) + 1, end = std::end(path);
             val != Json::Value::null && it != end;
             ++it) {
            val = val[*it];
        }

        if (val != Json::Value::null) {
            return this->jsonValueToStr(val);
        }

        return "";
    };

    if (section == "creative") {
        return [getter](const Context & context) {
            return getter(context.creative.toJson(), context);
        };
    } else if (section == "bidrequest") {
        return [getter](const Context & context) {
            return getter(context.bidrequest.toJson(), context);
        };
    } else if (section == "meta") {
        return [this, getter](const Context & context) {
            Json::Reader reader;
            Json::Value val;
            if (!reader.parse(context.response.meta.rawString(), val)) {
                std::cerr << "Failed to parse meta information for exchange:"
                          << this->exchange_
                          << ", meta: " << context.response.meta << std::endl;
            }

            return getter(val, context);
        };
    }

    throw std::runtime_error("Invalid variable: " + var.getVariable());
}


template <typename CreativeData>
typename CreativeConfiguration<CreativeData>::Expander
CreativeConfiguration<CreativeData>::generateExpander(
    const std::vector<ExpandVariable>& variables) const
{
    Expander expander;
    for (auto const& variable : variables) {
        auto callable = getAssociatedCallable(variable);

        ExpanderFilterCallable filterFn;

        auto const& filters = variable.getFilters();
        for (auto const& filter : filters) {
            auto it = filters_.find(filter);
            if (it == filters_.end()) {
                throw std::runtime_error("Invalid filter: " + filter);
            }

            auto currentFilter = it->second;
            if (filterFn) {
                filterFn = [currentFilter, filterFn](std::string& value)
                    -> std::string&
                {
                    return currentFilter(filterFn(value));
                };
            } else {
                filterFn = currentFilter;
            }
        }

        if (filterFn) {

            expander.addFunctor(
                    variable,
                    [filterFn, callable](Context const & ctx) {
                        std::string result = callable(ctx);
                        filterFn(result);
                        return result;
            });
        } else {
            expander.addFunctor(variable, callable);
        }
    }

    expander.finalize();
    return expander;
}

template <typename CreativeData>
std::string CreativeConfiguration<CreativeData>::jsonValueToStr(
    Json::Value const& val) const
{
    if (val.isUInt()) {
        return std::to_string(val.asUInt());
    }

    if (val.isIntegral()) {
        return std::to_string(val.asInt());
    }

    if (val.isString()) {
        return val.asString();
    }

    std::cerr << exchange_ << ": Cannot convert json value : " << val.toString()
              << " to string. Actual type: " << val.type() << " not supported"
              << std::endl;

    return "";
}

template <typename CreativeData>
std::string
CreativeConfiguration<CreativeData>::expand(const std::string& templateString,
                                            const Context& context) const
{
    boost::shared_lock<boost::shared_mutex> lock(mutex_);
    auto const& expander = expanders_[templateString];
    return expander.expand(templateString, context);
}

template <typename CreativeData>
struct CreativeConfiguration<CreativeData>::Expander
{
    typedef std::vector<std::pair<ExpandVariable, ExpanderCallable>> FunctorCollection;

    void addFunctor(const ExpandVariable& var, ExpanderCallable fn)
    {
        collection.push_back(std::make_pair(var, fn));
    }

    void finalize()
    {
        std::reverse(collection.begin(), collection.end());
    }

    std::string expand(std::string toExpand, const Context& ctx) const
    {
        for (auto& element : collection) {
            auto const& var = element.first;
            auto const& fn = element.second;

            auto const& location = var.getReplaceLocation();

            toExpand.replace(
                location.first, location.second - location.first, fn(ctx));
        }

        return toExpand;
    }

    FunctorCollection collection;

};

} // namespace RTBKIT
