#pragma once

#include <memory>
#include <string>
#include <vector>

// 对业务隐藏第三方解析库的一层封装。只在本头文件暴露字符串和整数。
class Json {
public:
    // 造一个空对象。无参数。
    Json();

    // 深拷贝。o：源。
    Json(const Json& o);

    // 搬走内部数据。无额外含义。
    Json(Json&&) noexcept;

    // 深拷贝赋值。o：源。返回自身。
    Json& operator=(const Json& o);

    // 移动赋值。返回自身。
    Json& operator=(Json&&) noexcept;

    // 释放内部对象。无参数。无返回值。
    ~Json();

    // 从文本解析。
    // s：一整段文本。
    // 返回：解析失败时 ok() 为 false。
    static Json parse(const std::string& s);

    // 造一个空的键值对象。无参数。
    static Json object();

    // 造一个空数组。无参数。
    static Json array();

    // 造一个字符串值。s：内容。
    static Json str(const std::string& s);

    // 造一个整数值。v：内容。
    static Json integer(int v);

    // 造一个真假值。v：内容。
    static Json boolean(bool v);

    // 是否是一份可用的对象或数组（不是解析失败后的空）。无参数。返回是/否。
    bool ok() const;

    // 编成一段文本发出去。无参数。返回文本。
    std::string dump() const;

    // 对象里是否有这个键。k：键名。返回是/否。
    bool contains(const std::string& k) const;

    // 取字符串。k：键名。def：没有或类型不对时的退路。返回字符串。
    std::string get_str(const std::string& k, const std::string& def = "") const;

    // 取整数。k：键名。def：没有或不是整数时的退路。返回整数。
    int get_int(const std::string& k, int def = 0) const;

    // 取真假。k：键名。def：没有或类型不对时的退路。返回真假。
    bool get_bool(const std::string& k, bool def = false) const;

    // 取整数数组。k：键名。返回该键下的整数列表；没有则为空。
    std::vector<int> get_ints(const std::string& k) const;

    // 写入一个子对象。k：键名。v：值。返回自身以便连写。
    Json& set(const std::string& k, const Json& v);

    // 写入字符串。k：键名。v：值。返回自身。
    Json& set(const std::string& k, const std::string& v);

    // 写入整数。k：键名。v：值。返回自身。
    Json& set(const std::string& k, int v);

    // 写入真假。k：键名。v：值。返回自身。
    Json& set(const std::string& k, bool v);

    // 把该键写成空。k：键名。返回自身。
    Json& set_null(const std::string& k);

    // 往数组末尾追加一项。v：要追加的值。无返回值。
    void push(const Json& v);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
