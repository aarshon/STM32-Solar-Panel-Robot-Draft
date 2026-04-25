/*
 * arm_motion.h  —  high-level arm motion API
 * Board : STM32F767ZI Nucleo (Arm/EE node, ADDR_ARM_EE in SRR-CP)
 *
 * Bridge between the SRR-CP command handlers (comm_arm_handlers.c) and
 * Tyler's low-level stepper / kinematics code. Tyler refines the bodies in
 * arm_motion.c — protocol code only sees this header.
 *
 * Joint indexing (matches kinematics.c):
 *   0 = t1 (base yaw)        — motor1 (6:1 gear)
 *   1 = t2 (shoulder pitch)  — motor2 (20:1 gear)
 *   2 = t3 (elbow pitch)     — motor3 (10:1 gear)
 */

#ifndef ARM_MOTION_H
#define ARM_MOTION_H

#include <stdint.h>

/* --- Cartesian + joint targets ----------------------------------------- */

/** Set Cartesian target in mm. Internally calls IK_3DOF, clamps joint
 *  angles, and updates each motor's targetSteps. Returns 1 if reachable,
 *  0 if rejected (out of envelope). */
uint8_t ArmMotion_SetTargetXYZ_mm(int16_t x_mm, int16_t y_mm, int16_t z_mm);

/** Set a single joint angle directly (degrees). joint = 0..2. */
uint8_t ArmMotion_SetJointDeg(uint8_t joint, float deg);

/** Manual jog — joint 0..2, dir +1 / -1, speed 0..255 (driver-defined). */
void ArmMotion_Jog(uint8_t joint, int8_t dir, uint8_t speed);

/* --- State transitions -------------------------------------------------- */

/** Halt all step pulses immediately (does NOT disable the drivers — motors
 *  keep holding torque). Used by ESTOP_ASSERT. */
void ArmMotion_HaltAll(void);

/** Re-enable motion after a halt. Used by ESTOP_CLEAR. */
void ArmMotion_ResumeAll(void);

/** Zero the current step counters for the joints in mask (bit n = joint n).
 *  Use after a manual home or limit-switch hit. */
void ArmMotion_ZeroSteps(uint8_t joint_mask);

/** Start an automatic homing pass on the joints in mask. Non-blocking;
 *  query ArmMotion_IsHoming() to wait for completion. */
void ArmMotion_StartHoming(uint8_t joint_mask);
uint8_t ArmMotion_IsHoming(void);

/* --- Status queries ----------------------------------------------------- */

/** Current joint angles in degrees, written into deg[3]. */
void ArmMotion_GetJointsDeg(float deg[3]);

/** 1 if any motor is still chasing its target. */
uint8_t ArmMotion_IsBusy(void);

/** Last motion fault (0 = none). Cleared on ArmMotion_ResumeAll(). */
uint8_t ArmMotion_GetFault(void);

/* --- End effector ------------------------------------------------------- */

/** Set EE torque/grip strength. Sign = direction; magnitude = strength.
 *  Tyler defines actuator semantics. */
void ArmMotion_EE_SetTorque(int8_t torque);

/** Pulse the EE actuator for duration_ms milliseconds (e.g. solenoid). */
void ArmMotion_EE_Pulse(uint16_t duration_ms);

#endif /* ARM_MOTION_H */
