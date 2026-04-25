#include <math.h>

#define L0 10.0f
#define L1 16.3f
#define L2 17.0f

void IK_3DOF(float x, float y, float z,
             float* t1, float* t2, float* t3)
{
    *t1 = atan2f(y, x);

    float r = sqrtf(x*x + y*y);
    float z_local = z - L0;

    float D = (r*r + z_local*z_local - L1*L1 - L2*L2) / (2 * L1 * L2);

    if (D > 1.0f) D = 1.0f;
    if (D < -1.0f) D = -1.0f;

    float s3 = sqrtf(1 - D*D);
    *t3 = atan2f(s3, D);

    float k1 = L1 + L2 * D;
    float k2 = L2 * s3;

    *t2 = atan2f(z_local, r) - atan2f(k2, k1);
}
