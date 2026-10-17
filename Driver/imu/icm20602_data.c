#include "icm20602.h"

static int16_t SignedBE(const uint8_t *p)
{
    uint32_t u = ((uint32_t)p[0] << 8) | p[1];
    /* Avoid implementation-defined unsigned-to-signed out-of-range casts. */
    int32_t s = (u >= 32768U) ? (int32_t)u - 65536 : (int32_t)u;
    return (int16_t)s;
}

void Icm20602_Decode(const uint8_t bytes[14], Icm20602Raw *out)
{
    Icm20602Raw raw;
    raw.ax = SignedBE(bytes);
    raw.ay = SignedBE(bytes + 2);
    raw.az = SignedBE(bytes + 4);
    raw.temp = SignedBE(bytes + 6);
    raw.gx = SignedBE(bytes + 8);
    raw.gy = SignedBE(bytes + 10);
    raw.gz = SignedBE(bytes + 12);
    *out = raw;
}

void Icm20602_ToUnits(const Icm20602Raw *raw, ImuSample *out)
{
    out->ax_g = (float)raw->ax / 16384.0f;
    out->ay_g = (float)raw->ay / 16384.0f;
    out->az_g = (float)raw->az / 16384.0f;
    out->gx_dps = (float)raw->gx / 131.0f;
    out->gy_dps = (float)raw->gy / 131.0f;
    out->gz_dps = (float)raw->gz / 131.0f;
}
