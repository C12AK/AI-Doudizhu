#include "json_util.h"

#include <nlohmann/json.hpp>

struct Json::Impl {
    nlohmann::json j;
};

Json::Json() : impl_(std::make_unique<Impl>()) {
    impl_->j = nlohmann::json::object();
}

Json::Json(const Json& o) : impl_(std::make_unique<Impl>()) {
    impl_->j = o.impl_->j;
}

Json::Json(Json&&) noexcept = default;

Json& Json::operator=(const Json& o) {
    if (this != &o) {
        impl_ = std::make_unique<Impl>();
        impl_->j = o.impl_->j;
    }
    return *this;
}

Json& Json::operator=(Json&&) noexcept = default;

Json::~Json() = default;

Json Json::parse(const std::string& s) {
    Json out;
    try {
        out.impl_->j = nlohmann::json::parse(s);
    } catch (...) {
        // 用空值表示解析失败，ok() 会因此为 false。
        out.impl_->j = nullptr;
    }
    return out;
}

Json Json::object() {
    Json o;
    o.impl_->j = nlohmann::json::object();
    return o;
}

Json Json::array() {
    Json o;
    o.impl_->j = nlohmann::json::array();
    return o;
}

Json Json::str(const std::string& s) {
    Json o;
    o.impl_->j = s;
    return o;
}

Json Json::integer(int v) {
    Json o;
    o.impl_->j = v;
    return o;
}

Json Json::boolean(bool v) {
    Json o;
    o.impl_->j = v;
    return o;
}

bool Json::ok() const {
    return impl_ && !impl_->j.is_null() && !impl_->j.is_discarded();
}

std::string Json::dump() const {
    return impl_->j.dump();
}

bool Json::contains(const std::string& k) const {
    return impl_->j.is_object() && impl_->j.contains(k);
}

std::string Json::get_str(const std::string& k, const std::string& def) const {
    if (!contains(k) || !impl_->j[k].is_string()) {
        return def;
    }
    return impl_->j[k].get<std::string>();
}

int Json::get_int(const std::string& k, int def) const {
    if (!contains(k)) {
        return def;
    }
    if (impl_->j[k].is_number_integer()) {
        return impl_->j[k].get<int>();
    }
    return def;
}

bool Json::get_bool(const std::string& k, bool def) const {
    if (!contains(k) || !impl_->j[k].is_boolean()) {
        return def;
    }
    return impl_->j[k].get<bool>();
}

std::vector<int> Json::get_ints(const std::string& k) const {
    std::vector<int> out;
    if (!contains(k) || !impl_->j[k].is_array()) {
        return out;
    }

    for (const auto& x : impl_->j[k]) {
        if (x.is_number_integer()) {
            out.push_back(x.get<int>());
        }
    }

    return out;
}

Json& Json::set(const std::string& k, const Json& v) {
    impl_->j[k] = v.impl_->j;
    return *this;
}

Json& Json::set(const std::string& k, const std::string& v) {
    impl_->j[k] = v;
    return *this;
}

Json& Json::set(const std::string& k, int v) {
    impl_->j[k] = v;
    return *this;
}

Json& Json::set(const std::string& k, bool v) {
    impl_->j[k] = v;
    return *this;
}

Json& Json::set_null(const std::string& k) {
    impl_->j[k] = nullptr;
    return *this;
}

void Json::push(const Json& v) {
    if (!impl_->j.is_array()) {
        impl_->j = nlohmann::json::array();
    }
    impl_->j.push_back(v.impl_->j);
}
