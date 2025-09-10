/*
 * OpenAI Configuration and Prompts
 * 
 * This file contains all OpenAI AI configuration including
 * prompts, function names, parameters, and message templates.
 * Unified in one place for better maintainability.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// VISION FUNCTION CONFIGURATION
// ============================================================================

/**
 * @brief Vision function name description for OpenAI tools
 */
/**
 * @brief Vision function name description for OpenAI tools
 */
#define VISION_FUNCTION_NAME "look_around"
#define VISION_FUNCTION_DESCRIPTION "Gets a detailed description of the user's current field of view."
/**
 * @brief Vision parameter name and description
 */
#define VISION_PARAM_NAME "visual_query"
#define VISION_PARAM_DESCRIPTION "The exact and literal question asked by the user so that the vision system knows which element of the scene to focus its analysis on. For example: 'What does that sign say?', 'What color is that chair?'."

// ============================================================================
// INSTRUCTIONS
// ============================================================================
/**
 * @brief Instructions with audio and vision capabilities
 */
#define INSTRUCTIONS_AUDIO_VISION "You are Jarvis, a AI resident in your wearer's glasses (mode audio)."

#ifdef __cplusplus
}
#endif
