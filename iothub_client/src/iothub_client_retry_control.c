#include "iothub_client_retry_control.h"
#include "azure_c_shared_utility/optimize_size.h"

#define RESULT_OK           0
#define INDEFINITE_TIME     ((time_t)-1)

typedef struct RETRY_CONTROL_INSTANCE_TAG
{
	IOTHUB_CLIENT_RETRY_POLICY policy_name;
	unsigned int max_retry_time_in_secs;

	unsigned int initial_wait_time_in_secs;
	double max_jitter_percent;

	unsigned int retry_count;
	time_t initial_retry_time;
	time_t last_retry_time;
	unsigned int current_wait_time_in_secs;
} RETRY_CONTROL_INSTANCE;

typedef int (*RETRY_ACTION_EVALUATION_FUNCTION)(RETRY_CONTROL_INSTANCE *retry_state, RETRY_ACTION *retry_action);


// ========== Helper Functions ========== //

static int evaluate_retry_action_fixed_interval(RETRY_CONTROL_INSTANCE *retry_state, RETRY_ACTION *retry_action)
{
	int result;

	time_t current_time;

	if ((current_time = get_time(NULL)) == INDEFINITE_TIME)
	{
		LogError("Cannot evaluate if should retry (get_time failed)");
		result = __FAILURE__;
	}
	else
	{
		if (retry_state->policy->max_retry_time_secs > 0 && 
			get_difftime(current_time, retry_state->start_time) >= retry_state->policy->max_retry_time_secs)
		{
			*retry_action = RETRY_ACTION_STOP_RETRYING;
		}
		else
		{
			if (get_difftime(current_time, retry_state->last_retry_time) >= retry_state->current_wait_time_in_secs)
			{
				*retry_action = RETRY_ACTION_RETRY_NOW;
				retry_state->last_retry_time = get_time(NULL);
			}
			else
			{
				*retry_action = RETRY_ACTION_RETRY_LATER;
			}
		}

		result = RESULT_OK;
	}

	return result;
}

static RETRY_ACTION_EVALUATION_FUNCTION get_retry_action_evaluation_function(IOTHUB_CLIENT_RETRY_POLICY policy_name)
{
	RETRY_ACTION_EVALUATION_FUNCTION result;

	if (policy_name == IOTHUB_CLIENT_RETRY_INTERVAL)
	{
		result = evaluate_retry_action_fixed_interval;
	}
	else
	{
		result = NULL;
	}

	return result;
}


// ========== Public API ========== //

int is_timeout_reached(time_t start_time, unsigned int timeout_in_secs, bool *is_timed_out)
{
	int result;

	if (start_time == INDEFINITE_TIME)
	{
		LogError("Failed to verify timeout (start_time is INDEFINITE)");
		result = __FAILURE__;
	}
	else
	{
		time_t current_time;

		if ((current_time = get_time(NULL)) == INDEFINITE_TIME)
		{
			LogError("Failed to verify timeout (get_time failed)");
			result = __FAILURE__;
		}
		else
		{
			if (get_difftime(current_time, start_time) >= timeout_in_secs)
			{
				*is_timed_out = true;
			}
			else
			{
				*is_timed_out = false;
			}

			result = RESULT_OK;
		}
	}

	return result;
}

RETRY_CONTROL_HANDLE retry_control_create(IOTHUB_CLIENT_RETRY_POLICY policy_name, unsigned int max_retry_time_in_secs)
{
	RETRY_CONTROL_INSTANCE* retry_control;

	if (max_retry_time_in_secs < 1)
	{
		LogError("Failed creating the retry control with start time (max_retry_time_in_secs must be at least 1 second)");
		retry_control = NULL;
	}
	else if ((retry_control = (RETRY_CONTROL_INSTANCE*)malloc(sizeof(RETRY_CONTROL_INSTANCE))) == NULL)
	{
		LogError("Failed creating the retry control with start time (malloc failed)");
	}
	else
	{
		memset(retry_control, 0, sizeof(RETRY_CONTROL_INSTANCE));
		retry_control->policy_name = policy_name;
		retry_control->max_retry_time_in_secs = max_retry_time_in_secs;
		retry_control->last_retry_time = INDEFINITE_TIME;
		retry_control->initial_wait_time_in_secs = 1;
	}

	return (RETRY_CONTROL_HANDLE)retry_control;
}

void retry_control_destroy(RETRY_CONTROL_HANDLE state_handle)
{
	if (state_handle != NULL)
	{
		free(state_handle);
	}
}

int retry_control_should_retry(RETRY_CONTROL_HANDLE retry_control_handle, RETRY_ACTION *retry_action)
{
	int result;

	if (retry_control_handle == NULL)
	{
		LogError("Failed to evaluate if retry should be attempted (retry_state_handle is NULL)");
		result = __FAILURE__;
	}
	else if (retry_action == NULL)
	{
		LogError("Failed to evaluate if retry should be attempted (retry_action is NULL)");
		result = __FAILURE__;
	}
	else
	{
		RETRY_CONTROL_INSTANCE* retry_control = (RETRY_CONTROL_INSTANCE*)retry_control_handle;

		RETRY_ACTION_EVALUATION_FUNCTION evaluate_retry_action = get_retry_action_evaluation_function(retry_control->policy_name);

		if (evaluate_retry_action == NULL)
		{
			LogError("Cannot evaluate if should retry (do not support policy %s)", retry_control->policy_name);
			result = __FAILURE__;
		}
		else if ((result = evaluate_retry_action(retry_control, retry_action)) != RESULT_OK)
		{
			LogError("Failed to evaluate if retry should be attempted (failed to evaluate fixed interval retry action)");
			result = __FAILURE__;
		}
		else
		{
			result = RESULT_OK;
		}
	}

	return result;
}

void retry_control_reset(RETRY_CONTROL_HANDLE retry_control_handle)
{
	if (retry_control_handle == NULL)
	{
		LogError("Failed to reset the retry control (retry_state_handle is NULL)");
	}
	else
	{
		RETRY_CONTROL_INSTANCE* retry_control = (RETRY_CONTROL_INSTANCE*)retry_control_handle;
		retry_control->retry_count = 0;
		retry_control->current_wait_time_in_secs = 0;
		retry_control->last_retry_time = INDEFINITE_TIME;
	}
}

int retry_control_set_option(RETRY_CONTROL_HANDLE retry_control_handle, const char* name, const void* value)
{
	int result;

	if (retry_control_handle == NULL)
	{
		LogError("Failed to set retry control option (either retry_state_handle (%p), name (%p) and/or value (%p) are NULL)", retry_control_handle, name, value);
		result = __FAILURE__;
	}
	else
	{
		// initial_wait_time_in_secs
		// max_jitter_percent (0 <= x <= 1)
		result = RESULT_OK;
	}

	return result;
}

OPTIONHANDLER_HANDLE retry_control_retrieve_options(RETRY_CONTROL_HANDLE retry_control_handle)
{
	OPTIONHANDLER_HANDLE result;

	if (retry_control_handle == NULL)
	{
		LogError("Failed to retrieve the retry control options (retry_state_handle is NULL)");
		result = NULL;
	}
	else
	{
		
		// Create OPTIONHANDLER_HANDLE
		// Add options
	}

	return result;
}