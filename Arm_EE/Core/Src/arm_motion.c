/*
 * arm_motion.c  —  high-level arm motion API
 *
 * Thin glue between SRR-CP command handlers and Tyler's stepper/kinematics
 * code. Most bodies here are intentionally minimal; Tyler is expected to
 * add ramping, coordinated motion, homing logic, soft limits, EE driving,
 * and reachability checks. Where a body is just a placeholder it is marked
 * with TYLER: so it's easy to grep.
 */

#include "arm_motion.h"
#include "motor_config.h"
#include "stepper.h"
#include "kinematics.h"

#include <math.h>

/* --- Module state ------------------------------------------------------ */
static uint8_t s_halted        = 0;   /* set by ArmMotion_HaltAll */
static uint8_t s_homing_mask   = 0;   /* nonzero while homing in progress */
static uint8_t s_last_fault    = 0;   /* sticky until ResumeAll */

/* --- Cartesian / joint targets ---------------------------------------- */

uint8_t ArmMotion_SetTargetXYZ_mm(int16_t x_mm, int16_t y_mm, int16_t z_mm)
{
    if (s_halted) return 0;

    /* kinematics.c uses cm units (L0=10, L1=16.3, L2=17). Convert mm → cm. */
    float x = (float)x_mm / 10.0f;
    float y = (float)y_mm / 10.0f;
    float z = (float)z_mm / 10.0f;

    float t1, t2, t3;
    IK_3DOF(x, y, z, &t1, &t2, &t3);

    /* TYLER: add reachability + soft-limit clamp here. Reject and return 0
     *        if any |t_i| exceeds the joint mechanical envelope, or if the
     *        IK clamp at D=±1 indicates the point is past the workspace. */

    Stepper_GoToAngleDeg(&motor1, radToDeg(t1));
    Stepper_GoToAngleDeg(&motor2, radToDeg(t2));
    Stepper_GoToAngleDeg(&motor3, radToDeg(t3));
    return 1;
}

uint8_t ArmMotion_SetJointDeg(uint8_t joint, float deg)
{
    if (s_halted) return 0;
    StepperMotor *m = (joint == 0) ? &motor1 :
                      (joint == 1) ? &motor2 :
                      (joint == 2) ? &motor3 : 0;
    if (!m) return 0;

    /* TYLER: add per-joint soft-limit clamp here. */

    Stepper_GoToAngleDeg(m, deg);
    return 1;
}

void ArmMotion_Jog(uint8_t joint, int8_t dir, uint8_t speed)
{
    /* TYLER: define jog semantics. Suggested: nudge targetSteps by a small
     *        delta proportional to speed every call, while command remains
     *        active. For now this is a no-op stub. */
    (void)joint; (void)dir; (void)speed;
}

/* --- State transitions ------------------------------------------------ */

void ArmMotion_HaltAll(void)
{
    /* Freeze targets to current position so the ISR stops issuing pulses. */
    motor1.targetSteps = motor1.currentSteps;
    motor2.targetSteps = motor2.currentSteps;
    motor3.targetSteps = motor3.currentSteps;
    s_halted = 1;
}

void ArmMotion_ResumeAll(void)
{
    s_halted    = 0;
    s_last_fault = 0;
}

void ArmMotion_ZeroSteps(uint8_t joint_mask)
{
    if (joint_mask & 0x01) { motor1.currentSteps = 0; motor1.targetSteps = 0; }
    if (joint_mask & 0x02) { motor2.currentSteps = 0; motor2.targetSteps = 0; }
    if (joint_mask & 0x04) { motor3.currentSteps = 0; motor3.targetSteps = 0; }
}

void ArmMotion_StartHoming(uint8_t joint_mask)
{
    /* TYLER: implement homing pass here. Drive each masked joint toward its
     *        limit switch, stop on switch close, ZeroSteps that joint, then
     *        back off a few mm. Until that's in place, treat as instant
     *        zero-without-moving. */
    s_homing_mask = joint_mask;
    ArmMotion_ZeroSteps(joint_mask);
    s_homing_mask = 0;
}

uint8_t ArmMotion_IsHoming(void) { return s_homing_mask != 0; }

/* --- Status ----------------------------------------------------------- */

void ArmMotion_GetJointsDeg(float deg[3])
{
    deg[0] = radToDeg(stepsToAngleMotor(&motor1));
    deg[1] = radToDeg(stepsToAngleMotor(&motor2));
    deg[2] = radToDeg(stepsToAngleMotor(&motor3));
}

uint8_t ArmMotion_IsBusy(void)
{
    return (motor1.currentSteps != motor1.targetSteps) ||
           (motor2.currentSteps != motor2.targetSteps) ||
           (motor3.currentSteps != motor3.targetSteps);
}

uint8_t ArmMotion_GetFault(void) { return s_last_fault; }

/* --- End effector ----------------------------------------------------- */

void ArmMotion_EE_SetTorque(int8_t torque)
{
    /* TYLER: drive EE actuator pin/PWM. No hardware decided yet, so stub. */
    (void)torque;
}

void ArmMotion_EE_Pulse(uint16_t duration_ms)
{
    /* TYLER: pulse EE for duration_ms then release. Stub. */
    (void)duration_ms;
}
