#ifndef IOTHUB_CLIENT_RETRY_CONTROL
#define IOTHUB_CLIENT_RETRY_CONTROL

#include <stdlib.h>
#include <stdbool.h>
#include "azure_c_shared_utility/optionhandler.h"
#include "azure_c_shared_utility/umock_c_prod.h"
#include "iothub_client_ll.h"

typedef enum RETRY_ACTION_TAG
{
	RETRY_ACTION_RETRY_NOW,
	RETRY_ACTION_RETRY_LATER,
	RETRY_ACTION_STOP_RETRYING
} RETRY_ACTION;

typedef RETRY_CONTROL_INSTANCE* RETRY_CONTROL_HANDLE;

MOCKABLE_FUNCTION(, RETRY_CONTROL_HANDLE, retry_control_create, IOTHUB_CLIENT_RETRY_POLICY, policy_name, unsigned int, max_retry_time_in_secs);
MOCKABLE_FUNCTION(, int, retry_control_should_retry, RETRY_CONTROL_HANDLE, retry_control_handle, RETRY_ACTION*, retry_action);
MOCKABLE_FUNCTION(, void, retry_control_reset, RETRY_CONTROL_HANDLE, retry_control_handle);
MOCKABLE_FUNCTION(, int, retry_control_set_option, RETRY_CONTROL_HANDLE, retry_control_handle, const char*, name, const void*, value);
MOCKABLE_FUNCTION(, OPTIONHANDLER_HANDLE, retry_control_retrieve_options, RETRY_CONTROL_HANDLE, retry_control_handle);
MOCKABLE_FUNCTION(, void, retry_control_destroy, RETRY_CONTROL_HANDLE, retry_control_handle);

MOCKABLE_FUNCTION(, int, is_timeout_reached, time_t, start_time, unsigned int, timeout_in_secs, bool*, is_timed_out);

#endif IOTHUB_CLIENT_RETRY_CONTROL 