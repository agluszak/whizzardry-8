#pragma once

#include <string>
#include <utility>

class srClass {
public:
    srClass() = default;
    srClass(const srClass&) = default;
    srClass& operator=(const srClass&) = default;
    virtual ~srClass() = default;

    const std::string& getName() const
    {
        static const std::string anonymous = "anonymous";
        return name_.empty() ? anonymous : name_;
    }
    void setName(std::string name) { name_ = std::move(name); }
    int isNamed() const { return !name_.empty(); }

private:
    std::string name_;
};
