#pragma once

// VTABLE: SURRENDER 0x10075310 srFilter
// class srFilter
class SR_DLL_IMPORT srFilter {
public:
    /* The reconstruction leaves trivial construction and copying implicit.
       The explicit virtual destructor supplies the modeled destruction interface. */

    // FUNCTION: SURRENDER 0x100032B0
    // RECOMP: ??1srFilter@@UAE@XZ
    virtual ~srFilter() {}

    virtual double getWeight(double value) const = 0;
    virtual double getSupport() const = 0;
};

// VTABLE: SURRENDER 0x10075350 srBoxFilter
// class srBoxFilter
class SR_DLL_IMPORT srBoxFilter : public srFilter {
public:
    virtual double getWeight(double value) const override;
    virtual double getSupport() const override;
};

// VTABLE: SURRENDER 0x10075378 srBellFilter
// class srBellFilter
class SR_DLL_IMPORT srBellFilter : public srFilter {
public:
    virtual double getWeight(double value) const override;
    virtual double getSupport() const override;
};

// VTABLE: SURRENDER 0x10075398 srBSplineFilter
// class srBSplineFilter
class SR_DLL_IMPORT srBSplineFilter : public srFilter {
public:
    virtual double getWeight(double value) const override;
    virtual double getSupport() const override;
};

// VTABLE: SURRENDER 0x10075388 srTriangleFilter
// class srTriangleFilter
class SR_DLL_IMPORT srTriangleFilter : public srFilter {
public:
    virtual double getWeight(double value) const override;
    virtual double getSupport() const override;
};

extern SR_DLL_IMPORT class srBoxFilter srBoxFilter;
extern SR_DLL_IMPORT class srBellFilter srBellFilter;
extern SR_DLL_IMPORT class srBSplineFilter srBSplineFilter;
extern SR_DLL_IMPORT class srTriangleFilter srTriangleFilter;
