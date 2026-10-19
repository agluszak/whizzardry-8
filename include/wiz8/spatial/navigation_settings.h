#pragma once

// Each navigation mesh owns the tuning read with its level.
struct NavigationSettings {
    float acceleration_factor = 3.0f;
    float angular_acceleration_factor = 2.0f;
    float angular_deceleration_factor = 0.5f;
    float prediction_time = 1.0f;
    float approach_slow_time = 1.0f;
    float approach_run_time = 8.0f;
    float approach_run_rate = 2.0f;
    float group_repulsion_factor = 1.0f;
    float party_boundary_radius = 2.0f;
    float obstacle_steering_factor = 2.0f;
    float obstacle_braking_factor = 0.3f;
    float minimum_animation_rate = 0.5f;
    float flying_swimming_minimum_animation_rate = 0.8999999761581421f;
};
