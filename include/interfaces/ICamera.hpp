#pragma once

class ICamera {
public:
    virtual ~ICamera() = default;
    virtual void getViewMatrix(float out4x4[16])       const = 0;
    virtual void getProjMatrix(float out4x4[16], float aspect) const = 0;
    virtual void getPosition(float out3[3])            const = 0;
    virtual float getYaw()   const = 0;
    virtual float getPitch() const = 0;
};
