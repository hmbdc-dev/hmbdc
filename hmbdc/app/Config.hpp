#include "hmbdc/Copyright.hpp"
#pragma once


#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <string>
#include <memory>
#include <list>
#include <vector>
#include <unordered_set>
#include <sstream>


/**
 * @namespace hmbdc::app
 * hmbdc's application layer where API resides
 */
namespace hmbdc { namespace app { 

namespace config_detail {

/**
 * @brief class to hold an hmbdc configuration
 * @details it is based on a two level (fallback and section) json. 
 * top level for the fallback values and lower level for the section specific values.
 * a Config instance is always constructed to be associated to 0 or 1 specific section.
 * shown below:
 * 
 *      {
 *          "parameter_1": "top level, value used as a fallback",
 *          "parameter_2": "fallback is used when not configured in a section",
 *          "section_A": {
 *               "parameter_2": "lower level, a specific value effective in section_A",
 *               "parameter_1": "a specific value effective in section_A"
 *          },
 *          "another_section": {
 *               "parameter_2": "a specific value effective in another_section"
 *          }
 *      }
 *  json array is not supported !!!
 */
struct Config 
: boost::property_tree::ptree {
    // using ptree::ptree;
    using Base = boost::property_tree::ptree;

    /**
     * @brief empty config
     */
    Config(){}

    /**
     * @brief copt ctor
     */
    Config(Config const& other)
    : Base(other)
    , section_(other.section_)
    , fallbackConfig_(other.fallbackConfig_
        ?new Config(*other.fallbackConfig_)
        :nullptr)
    {}

    /**
     * @brief assignment
     */
    Config& operator = (Config const& other) {
        (Base&)*this = other;
        section_ = other.section_;
        fallbackConfig_.reset(other.fallbackConfig_
            ?new Config(*other.fallbackConfig_)
            :nullptr);
        return *this;
    }

    /**
     * @brief construct using stream, optionally specifying the section name
     * @details if the section is nullptr, just use the fallback values. if
     * the section name cannot be found, throw an exception
     * 
     * @param is stream as input providing a json stream
     * @param section pointing to the effective section in the json above
     */
    explicit 
    Config(std::istream&& is, char const* section = nullptr)
    : section_(section?section:"") {
        read_json(is, (Base&)*this);
        get_child(section_);
    }

    /**
     * @brief construct using stream, optionally specifying the section name
     * @details if the section is nullptr, just use the fallback values. if
     * the section name cannot be found, throw an exception
     * 
     * @param is stream as input providing a json stream
     * @param section pointing to the effective section in the json above
     */
    explicit 
    Config(std::istream& is, char const* section = nullptr)
    : section_(section?section:"") {
        read_json(is, (Base&)*this);
        get_child(section_);
    }

    /**
     * @brief construct using a string, optionally specifying the section name
     * @details if the section is nullptr, just use the fallback values. if
     * the section name cannot be found, throw an exception
     * 
     * @param json string as input providing a json text
     * @param section pointing to the effective section in the json above
     */
    explicit 
    Config(char const* json, char const* section = nullptr)
    : Config(std::istringstream(json), section)
    {}

    /**
     * @brief construct using another ptree as fallbacks, optionally specifying the section name
     * @details if the section is nullptr, just use the fallback values. if
     * the section name cannot be found, throw an exception
     * 
     * @param t ptree as input providing fallbacks (and sections)
     * @param section pointing to the effective section in the ptree above
     */
    Config(boost::property_tree::ptree const& t, char const* section = nullptr)
    : Base(t)
    , section_(section?section:"") {
        get_child(section_);
    }

    /**
     * @brief construct using a fallback ptree, and specify the section ptree
     * @details if the section is nullptr, just use the fallback values. if
     * the section name cannot be found, throw an exception
     * 
     * @param dft ptree as input providing fallbacks
     * @param section ptree providing section specific values
     */
    Config(boost::property_tree::ptree const& dft, boost::property_tree::ptree const& section)
    : Base(section)
    , fallbackConfig_(new Config(dft)) {
    }

    /**
     * @brief set additional defaults
     * @details previously set defaults take precedence
     * 
     * @param c a config holding configuration values
     * @return *this
     */
    Config& setAdditionalFallbackConfig(Config const& c) {
        if (!fallbackConfig_) {
            fallbackConfig_.reset(new Config(c));
        } else {
            fallbackConfig_->setAdditionalFallbackConfig(c);
        }
        return *this;
    }
    /**
     * @brief depracated
     */
    void setDefaultUserConfig(Config const& c) {
        setAdditionalFallbackConfig(c);
    }

    /**
     * @brief change section name
     * @details the fallbacks also changes accordingly if possible
     * 
     * @param section new section name
     * @param sectionExists check if the section exist in current config
     * - exlcuding fallbacks if set to true
     * @return object itself
     */
    Config& resetSection(char const* section, bool sectionExists = true) {
        if (sectionExists) get_child(section);
        auto sec = section?section:"";
        if (get_child_optional(sec)) section_ = sec; //else no change
        if (fallbackConfig_) {
            fallbackConfig_->resetSection(section, false);
        }
        return *this;
    }
    
    /**
     * @brief forward the call to ptree's put but return Configure
     * 
     * @tparam Args whatever ptree put expect
     * @param args whatever ptree put expect
     * @return object itself
     */
    template <typename ...Args>
    Config& put(Args&&... args) {
        Base::put(std::forward<Args>(args)...);
        return *this;
    }

    /**
     * @brief put an array of values
     * 
     * @tparam Args whatever ptree put expect
     * @param args whatever ptree put expect
     * @return object itself
     */
    template <typename Array>
    Config& putArray(const path_type& param, Array&& array) {
        Base a;
        for (auto const& v : array) {
            Base elem;
            elem.put("", v);
            a.push_back(std::make_pair("", elem));
        }
        Base::put_child(param, a);
        return *this;
    }

    /**
     * @brief put an array of values
     * 
     * @param args a delimit seperated string for array values
     * @param delimit delimit such as "," or "[sep]"
     * @return object itself
     */
    Config& putArray(const path_type& param, std::string const& values
        , char const* delimit) {
        std::vector<std::string> array;
        auto s = values;
        while(true) {
            auto pos = s.find(delimit);
            auto v = s.substr(0, pos);
            if (v.size()) array.push_back(v);
            if (pos != std::string::npos) {
                s.erase(0,  pos + strlen(delimit));
            } else {
                break;
            }
        }
        return putArray(param, array);
    }

    /**
     * @brief      Gets the child from the config.
     * @details check the section for it, if not found, try use fallback provided
     * if still missing, search using the default user config values set by
     * setDefaultUserConfig. Throw exception ptree_bad_path if all fail
     *
     * @param[in]  param  config parameter name
     *
     * @return     ptree reference to the child.
     */
    boost::property_tree::ptree const& getChildExt(const path_type& param) const {
        auto sec = section_;
        auto res = get_child_optional(sec/=param);
        if (!res) {
            res = get_child_optional(param);
            if (!res) {
                if (fallbackConfig_) {
                    return fallbackConfig_->getChildExt(param);
                } else {
                    throw boost::property_tree::ptree_bad_path(
                        section_.dump() + ":invalid param and no default user Config set", param);
                }
            }
        }
        return *res;
    }

    /**
     * @brief get a value from the config
     * @details check the section for it, if not found, try use fallback provided
     * if still missing, search using the default user config values set by
     * setDefaultUserConfig. If throwIfMissing, throw exception ptree_bad_path if all fail
     * 
     * @param param config parameter name
     * @param throwIfMissing - true if no config found, throw an exception; otherwise return empty
     * @tparam T type of the value
     * @return result
     */
    template <typename T>
    T getExt(const path_type& param, bool throwIfMissing = true) const {
        auto sec = section_;
        auto res = get_optional<T>(sec/=param);
        if (!res) {
            res = get_optional<T>(param);
            if (!res) {
                if (get_child_optional(param)) {
                    throw boost::property_tree::ptree_bad_data(
                        section_.dump() + ":invalid data", param); 
                }
                if (fallbackConfig_) {
                    return fallbackConfig_->getExt<T>(param, throwIfMissing);
                } else if (throwIfMissing) {
                    throw boost::property_tree::ptree_bad_path(
                        section_.dump() + ":invalid param and no default user Config set", param); 
                } else {
                    return T{};
                }
            }
        }
        return *res; 
    }

    /**
     * @brief get a vector of value from the json array
     * @details check the section for it, if not found, try use fallback provided
     * if still missing, search using the default user config values set by
     * setDefaultUserConfig.
     * 
     * @param param config parameter name
     * @tparam T type of the value
     * @return result
     */
    template <typename T>
    std::vector<T> getArray(const path_type& param) const {
        std::vector<T> res;
        auto sec = section_;
        auto children = get_child_optional(sec/=param);
        if (!children) children = get_child_optional(param);
        if (children) {
            for (auto& v : *children) {
                if (v.first.empty()) {
                    // special case for [""] which reserved for an empty array
                    // due to ptree limits for json array
                    if (children->size() > 1
                        || v.second.get_value<std::string>() != "") {
                        res.push_back(v.second.get_value<T>());
                    }
                } else {
                    throw boost::property_tree::ptree_bad_data(
                        section_.dump() + ":param not pointing to array ", param);
                }
            }
            return res;
        } else if (fallbackConfig_) {
            return fallbackConfig_->getArray<T>(param);
        }
        throw boost::property_tree::ptree_bad_path(
            section_.dump() + ":invalid array param and no default user Config set", param);
    }

    /**
     * @brief get a number value in hex format
     * @details check the section for it, if not found, try use fallback provided
     * if still missing, search using the default user config values set by
     * setDefaultUserConfig. Throw exception ptree_bad_path if all fail
     * 
     * @param param config parameter name
     * @tparam T numeric type of the value: int , uint64_t ...
     * @param throwIfMissing - true if no config found, throw an exception
     * @return result
     */
    template <typename T>
    T getHex(boost::property_tree::ptree::path_type const& param
        , bool throwIfMissing = true) const {
        std::istringstream iss(getExt<std::string>(param, throwIfMissing));
        T res;
        iss >> std::hex >> res;
        return res;
    }
 
    /**
     * @brief fill in a variable with a configured value retrieved using getExt
     * @details example cfg(abc, "abc")(def, "def");
     * 
     * @param to destination
     * @param param config parameter
     * @param throwIfMissing - true if no config found, throw an exception
     * 
     * @return the Config object itself
     */
    template <typename T>
    Config const& operator()(T& to, const boost::property_tree::ptree::path_type& param
        , bool throwIfMissing = true) const {
        to = getExt<T>(param, throwIfMissing);
        return *this;
    }

    /**
     * @brief fill an unordered_set with a configured value retrieved using getExt
     * @details the value in the Config is a space separated string
     * 
     * @param to destination
     * @param param config parameter
     * @param throwIfMissing - true if no config found, throw an exception; otherwise ignored
     * 
     * @return the Config object itself
     */
    template <typename T>
    Config const& operator()(std::unordered_set<T>& to
        , const boost::property_tree::ptree::path_type& param, bool throwIfMissing = true) const {
        // auto toStr = getExt<string>(param);
        auto s = getExt<std::string>(param, throwIfMissing);
        std::istringstream iss(s);
   
        for (auto iit = std::istream_iterator<T>(iss)
            ; iit != std::istream_iterator<T>()
            ; iit ++) {
            to.insert(*iit);
        }
        if (!iss.eof()) {
            throw (boost::property_tree::ptree_bad_data(
                "not space separated items in Config ", param));
        }
   
        return *this;
    }

    /**
     * @brief fill an list with a configured value retrieved using getExt
     * @details the value in the Config is a space separated string
     * 
     * @param to destination
     * @param param config parameter
     * @param throwIfMissing - true if no config found, throw an exception; otherwise ignored
     * 
     * @return the Config object itself
     */
    template <typename T>
    Config const& operator()(std::vector<T>& to, const boost::property_tree::ptree::path_type& param
        , bool throwIfMissing = true) const {
        auto s = getExt<std::string>(param, throwIfMissing);
        std::istringstream iss(s);
   
        for (auto iit = std::istream_iterator<T>(iss)
            ; iit != std::istream_iterator<T>()
            ; iit ++) {
            to.emplace_back(*iit);
        }
        if (!iss.eof()) {
            throw (boost::property_tree::ptree_bad_data(
                section_.dump() + ":not space separated items in Config ", param));
        }
   
        return *this;
    }

    /**
     * @brief get contents of all  the effective configure in the form of list of string pairs
     * @details only effective ones are shown
     * 
     * @param skipThese skip those config params
     * @return list of string pairs in the original order of ptree nodes
     */
    std::list<std::pair<std::string, std::string>> content(
        std::unordered_set<std::string> const& skipThese = std::unordered_set<std::string>()) const {
         std::list<std::pair<std::string, std::string>> res;
         std::unordered_set<std::string> history(skipThese);
         auto secTree = get_child_optional(section_);
         if (secTree) {
             for (auto& p : *secTree) {
                if (history.find(p.first) == history.end()) {
                    history.insert(p.first);
                    if (p.second.empty()) { //leaf
                        res.push_back(make_pair(p.first, p.second.get_value<std::string>()));
                    }
                     // else { //or array
                     //    auto arrayText = getArrayText(p.second);
                     //    if (arrayText.size()) {
                     //        res.push_back(make_pair(p.first, arrayText));
                     //    }
                     // }
                }
             }
         }
         for (auto& p : *this) {
            if (history.find(p.first) == history.end()) {
                history.insert(p.first);
                if (p.second.empty()) { //leaf
                    res.push_back(make_pair(p.first, p.second.get_value<std::string>()));
                } 
                // else { // or array
                //     auto arrayText = getArrayText(p.second);
                //     if (arrayText.size()) {
                //         res.push_back(make_pair(p.first, arrayText));
                //     }
                // }
            }
         }
         if (fallbackConfig_) {
            auto more = fallbackConfig_->content(history);
            res.insert(res.end(), more.begin(), more.end());
         }
         return res;
    }
    
    /**
     * @brief      stream out the effective settings
     *
     * @param      os    ostream
     * @param      cfg   The configuration
     *
     * @return     os
     */
    friend std::ostream& operator << (std::ostream& os, Config const& cfg) {
        for (auto& r : cfg.content()) {
            os << r.first << '=' << r.second << std::endl;
        }
        if (cfg.fallbackConfig_) {
            os << "next in line fallback" << std::endl;
            os << *cfg.fallbackConfig_ << std::endl;
        }

        return os;
    }

    /**
     * @brief it's very commion a config needs to be updated from the cmd line
     * arg list via key=val args:
     * argv example: a_key=a_value a_array=a0,a1,a2 a.multi.level.path=some_value
     * if a arg in argv does not exit in the config, throw
     * @param argc how many args in argv; when return: how many args are not procesed 
     * since there is no "=" in them and they are not processed (not throw in these cases) 
     * - for example "--help"
     * @param argv paramaters, when return, it contains only unprecessed params
     * @param arrayDelimit when setting array use this str as delimit
     * @return *this
     */
    Config& updateWithCmdline(int& argc, char const* argv[], char const* arrayDelimit = ",")  {
        int unprocessed = 0;
        while (unprocessed != argc) {
            auto arg = std::string(argv[unprocessed]);
            auto pos = arg.find("=");
            if (pos == arg.npos) {
                unprocessed++;
                continue;
            }
            auto paramPath = arg.substr(0, pos);
            arg.erase(0,  pos + 1);
            auto const& child = getChildExt(paramPath);
            // is it array
            if (child.size() && child.begin()->first == "") {
                putArray(paramPath, arg, arrayDelimit);
            } else {
                put(paramPath, arg);
            }
            memmove(argv + unprocessed, argv + unprocessed + 1
                , sizeof(char*) * (argc - unprocessed - 1));
            --argc;
        }
        return *this;
    }

private:
    // std::string getArrayText(ptree const& pt) const {
    //     bool isArray = true;
    //     std::string arrayText("[");
    //     for (auto& q : pt) {
    //         if (!q.second.empty() || q.first.size() != 0) {
    //             isArray = false;
    //             break;
    //         }
    //         arrayText += "\"" + q.second.get_value<std::string>() + "\",";
    //     }
    //     *arrayText.rbegin() = ']';
    //     if (isArray) {
    //         return arrayText;
    //     } else {
    //         return std::string();
    //     }
    // }
    boost::property_tree::ptree::path_type section_;
    std::unique_ptr<Config> fallbackConfig_;
};
} //config_detail

using Config = config_detail::Config;
}}

