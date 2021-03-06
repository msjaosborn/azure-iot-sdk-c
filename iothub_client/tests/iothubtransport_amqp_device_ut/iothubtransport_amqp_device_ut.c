// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#ifdef __cplusplus
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdbool>
#include <cstdint>
#else
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#endif

void* real_malloc(size_t size)
{
    return malloc(size);
}

void real_free(void* ptr)
{
    free(ptr);
}

#include "testrunnerswitcher.h"
#include "azure_c_shared_utility/macro_utils.h"
#include "umock_c.h"
#include "umocktypes_charptr.h"
#include "umocktypes_bool.h"
#include "umocktypes_stdint.h"
#include "umock_c_negative_tests.h"
#include "umocktypes.h"
#include "umocktypes_c.h"

#define ENABLE_MOCKS

#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/gballoc.h"
#include "azure_c_shared_utility/agenttime.h" 
#include "azure_c_shared_utility/strings.h"
#include "azure_c_shared_utility/singlylinkedlist.h"
#include "azure_c_shared_utility/uniqueid.h"
#include "azure_uamqp_c/session.h"
#include "azure_uamqp_c/link.h"
#include "azure_uamqp_c/messaging.h"
#include "azure_uamqp_c/message_sender.h"
#include "azure_uamqp_c/message_receiver.h"
#include "iothub_client_private.h"
#include "iothub_client_version.h"
#include "iothub_message.h"
#include "uamqp_messaging.h"
#include "iothubtransport_amqp_messenger.h"
#include "iothubtransport_amqp_cbs_auth.h"

#undef ENABLE_MOCKS

#include "iothubtransport_amqp_device.h"


static TEST_MUTEX_HANDLE g_testByTest;
static TEST_MUTEX_HANDLE g_dllByDll;

DEFINE_ENUM_STRINGS(UMOCK_C_ERROR_CODE, UMOCK_C_ERROR_CODE_VALUES)

static void on_umock_c_error(UMOCK_C_ERROR_CODE error_code)
{
    char temp_str[256];
    (void)snprintf(temp_str, sizeof(temp_str), "umock_c reported error :%s", ENUM_TO_STRING(UMOCK_C_ERROR_CODE, error_code));
    ASSERT_FAIL(temp_str);
}


// ---------- Defines and Test Data ---------- //

// copied from iothubtransport_amqp_device.c
static const char* DEVICE_OPTION_SAVED_AUTH_OPTIONS = "saved_device_auth_options";
static const char* DEVICE_OPTION_SAVED_MESSENGER_OPTIONS = "saved_device_messenger_options";
#define DEFAULT_AUTH_STATE_CHANGED_TIMEOUT_SECS    60
#define DEFAULT_MSGR_STATE_CHANGED_TIMEOUT_SECS    60

#define INDEFINITE_TIME                                   ((time_t)-1)
#define TEST_DEVICE_ID_CHAR_PTR                           "bogus-device"
#define TEST_IOTHUB_HOST_FQDN_CHAR_PTR                    "thisisabogus.azure-devices.net"
#define TEST_ON_STATE_CHANGED_CONTEXT                     (void*)0x7710
#define TEST_USER_DEFINED_SAS_TOKEN                       "blablabla"
#define TEST_USER_DEFINED_SAS_TOKEN_STRING_HANDLE         (STRING_HANDLE)0x7711
#define TEST_PRIMARY_DEVICE_KEY                           "MUhT4tkv1auVqZFQC0lyuHFf6dec+ZhWCgCZ0HcNPuW="
#define TEST_PRIMARY_DEVICE_KEY_STRING_HANDLE             (STRING_HANDLE)0x7712
#define TEST_SECONDARY_DEVICE_KEY                         "WCgCZ0HcNPuWMUhTdec+ZhVqZFQC4tkv1auHFf60lyu="
#define TEST_SECONDARY_DEVICE_KEY_STRING_HANDLE           (STRING_HANDLE)0x7713
#define TEST_AUTHENTICATION_HANDLE                        (AUTHENTICATION_HANDLE)0x7714
#define TEST_MESSENGER_HANDLE                             (MESSENGER_HANDLE)0x7715
#define TEST_GENERIC_CHAR_PTR                             "some generic text"
#define TEST_STRING_HANDLE                                (STRING_HANDLE)0x7716
#define TEST_SESSION_HANDLE                               (SESSION_HANDLE)0x7717
#define TEST_CBS_HANDLE                                   (CBS_HANDLE)0x7718
#define TEST_IOTHUB_MESSAGE_HANDLE                        (IOTHUB_MESSAGE_HANDLE)0x7719
#define TEST_VOID_PTR                                     (void*)0x7720
#define TEST_OPTIONHANDLER_HANDLE                         (OPTIONHANDLER_HANDLE)0x7721
#define TEST_AUTH_OPTIONHANDLER_HANDLE                    (OPTIONHANDLER_HANDLE)0x7722
#define TEST_MSGR_OPTIONHANDLER_HANDLE                    (OPTIONHANDLER_HANDLE)0x7723
#define TEST_ON_DEVICE_EVENT_SEND_COMPLETE_CONTEXT        (void*)0x7724
#define TEST_IOTHUB_MESSAGE_LIST                          (IOTHUB_MESSAGE_LIST*)0x7725

static time_t TEST_current_time;


// ---------- Time-related Test Helpers ---------- //

static time_t add_seconds(time_t base_time, unsigned int seconds)
{
	time_t new_time;
	struct tm *bd_new_time;

	if ((bd_new_time = localtime(&base_time)) == NULL)
	{
		new_time = INDEFINITE_TIME;
	}
	else
	{
		bd_new_time->tm_sec += seconds;
		new_time = mktime(bd_new_time);
	}

	return new_time;
}


// ---------- Test Hooks ---------- //

static int saved_malloc_returns_count = 0;
static void* saved_malloc_returns[20];

static void* TEST_malloc(size_t size)
{
	saved_malloc_returns[saved_malloc_returns_count] = real_malloc(size);

	return saved_malloc_returns[saved_malloc_returns_count++];
}

static void TEST_free(void* ptr)
{
	int i, j;
	for (i = 0, j = 0; j < saved_malloc_returns_count; i++, j++)
	{
		if (saved_malloc_returns[i] == ptr)
		{
			real_free(ptr); // only free what we have allocated. Trick to help on TEST_mallocAndStrcpy_s.
			j++;
		}

		saved_malloc_returns[i] = saved_malloc_returns[j];
	}

	if (i != j) saved_malloc_returns_count--;
}

static int TEST_mallocAndStrcpy_s_return;
static int TEST_mallocAndStrcpy_s(char** destination, const char* source)
{
	*destination = (char*)source;
	return TEST_mallocAndStrcpy_s_return;
}

static MESSENGER_HANDLE TEST_messenger_subscribe_for_messages_saved_messenger_handle;
static ON_MESSENGER_MESSAGE_RECEIVED TEST_messenger_subscribe_for_messages_saved_on_message_received_callback;
static void* TEST_messenger_subscribe_for_messages_saved_context;
static int TEST_messenger_subscribe_for_messages_return;
static int TEST_messenger_subscribe_for_messages(MESSENGER_HANDLE messenger_handle, ON_MESSENGER_MESSAGE_RECEIVED on_message_received_callback, void* context)
{
	TEST_messenger_subscribe_for_messages_saved_messenger_handle = messenger_handle;
	TEST_messenger_subscribe_for_messages_saved_on_message_received_callback = on_message_received_callback;
	TEST_messenger_subscribe_for_messages_saved_context = context;

	return TEST_messenger_subscribe_for_messages_return;
}

static double TEST_get_difftime(time_t end_time, time_t start_time)
{
	return difftime(end_time, start_time);
}

static ON_AUTHENTICATION_STATE_CHANGED_CALLBACK TEST_authentication_create_saved_on_authentication_changed_callback;
static void* TEST_authentication_create_saved_on_authentication_changed_context;
static ON_AUTHENTICATION_ERROR_CALLBACK TEST_authentication_create_saved_on_error_callback;
static void* TEST_authentication_create_saved_on_error_context;
static AUTHENTICATION_HANDLE TEST_authentication_create_return;
static AUTHENTICATION_HANDLE TEST_authentication_create(const AUTHENTICATION_CONFIG* config)
{
	TEST_authentication_create_saved_on_authentication_changed_callback = config->on_state_changed_callback;
	TEST_authentication_create_saved_on_authentication_changed_context = config->on_state_changed_callback_context;
	TEST_authentication_create_saved_on_error_callback = config->on_error_callback;
	TEST_authentication_create_saved_on_error_context = config->on_error_callback_context;
	return TEST_authentication_create_return;
}

static ON_MESSENGER_STATE_CHANGED_CALLBACK TEST_messenger_create_saved_on_state_changed_callback;
static void* TEST_messenger_create_saved_on_state_changed_context;
static MESSENGER_HANDLE TEST_messenger_create_return;
static MESSENGER_HANDLE TEST_messenger_create(const MESSENGER_CONFIG *config)
{
	TEST_messenger_create_saved_on_state_changed_callback = config->on_state_changed_callback;
	TEST_messenger_create_saved_on_state_changed_context = config->on_state_changed_context;
	return TEST_messenger_create_return;
}

static IOTHUB_MESSAGE_LIST* TEST_messenger_send_async_saved_message;
static ON_MESSENGER_EVENT_SEND_COMPLETE TEST_messenger_send_async_saved_callback;
static void* TEST_messenger_send_async_saved_context;
static int TEST_messenger_send_async_result;
static int TEST_messenger_send_async(MESSENGER_HANDLE messenger_handle, IOTHUB_MESSAGE_LIST* message, ON_MESSENGER_EVENT_SEND_COMPLETE on_messenger_event_send_complete_callback, void* context)
{
	(void)messenger_handle;
	TEST_messenger_send_async_saved_message = message;
	TEST_messenger_send_async_saved_callback = on_messenger_event_send_complete_callback;
	TEST_messenger_send_async_saved_context = context;
	return TEST_messenger_send_async_result;
}

// ---------- Test Callbacks ---------- //

static void* TEST_on_state_changed_callback_saved_context;
static DEVICE_STATE TEST_on_state_changed_callback_saved_previous_state;
static DEVICE_STATE TEST_on_state_changed_callback_saved_new_state;
static void TEST_on_state_changed_callback(void* context, DEVICE_STATE previous_state, DEVICE_STATE new_state)
{
	TEST_on_state_changed_callback_saved_context = context;
	TEST_on_state_changed_callback_saved_previous_state = previous_state;
	TEST_on_state_changed_callback_saved_new_state = new_state;
}

static IOTHUB_MESSAGE_HANDLE TEST_on_message_received_saved_message;
static void* TEST_on_message_received_saved_context;
static DEVICE_MESSAGE_DISPOSITION_RESULT TEST_on_message_received_return;
static DEVICE_MESSAGE_DISPOSITION_RESULT TEST_on_message_received(IOTHUB_MESSAGE_HANDLE message, void* context)
{
	TEST_on_message_received_saved_message = message;
	TEST_on_message_received_saved_context = context;

	return TEST_on_message_received_return;
}

static IOTHUB_MESSAGE_LIST* TEST_on_device_d2c_event_send_complete_callback_saved_message;
static D2C_EVENT_SEND_RESULT TEST_on_device_d2c_event_send_complete_callback_saved_result;
static void* TEST_on_device_d2c_event_send_complete_callback_saved_context;
static void TEST_on_device_d2c_event_send_complete_callback(IOTHUB_MESSAGE_LIST* message, D2C_EVENT_SEND_RESULT result, void* context)
{
	TEST_on_device_d2c_event_send_complete_callback_saved_message = message;
	TEST_on_device_d2c_event_send_complete_callback_saved_result = result;
	TEST_on_device_d2c_event_send_complete_callback_saved_context = context;
}


// ---------- Test Helpers ---------- //

static DEVICE_CONFIG TEST_device_config;
static DEVICE_CONFIG* get_device_config(DEVICE_AUTH_MODE auth_mode, bool use_device_keys)
{
	memset(&TEST_device_config, 0, sizeof(DEVICE_CONFIG));
	TEST_device_config.device_id = TEST_DEVICE_ID_CHAR_PTR;
	TEST_device_config.iothub_host_fqdn = TEST_IOTHUB_HOST_FQDN_CHAR_PTR;
	TEST_device_config.authentication_mode = auth_mode;
	TEST_device_config.on_state_changed_callback = TEST_on_state_changed_callback;
	TEST_device_config.on_state_changed_context = TEST_ON_STATE_CHANGED_CONTEXT;

	if (use_device_keys)
	{
		TEST_device_config.device_primary_key = TEST_PRIMARY_DEVICE_KEY;
		TEST_device_config.device_secondary_key = TEST_SECONDARY_DEVICE_KEY;
	}
	else
	{
		TEST_device_config.device_sas_token = TEST_USER_DEFINED_SAS_TOKEN;
	}

	return &TEST_device_config;
}

static void initialize_test_variables()
{
	saved_malloc_returns_count = 0;

	TEST_mallocAndStrcpy_s_return = 0;

	TEST_on_state_changed_callback_saved_context = NULL;
	TEST_on_state_changed_callback_saved_previous_state = DEVICE_STATE_STOPPED;
	TEST_on_state_changed_callback_saved_new_state = DEVICE_STATE_STOPPED;

	TEST_current_time = time(NULL);

	TEST_on_message_received_saved_message = NULL;
	TEST_on_message_received_saved_context = NULL;
	TEST_on_message_received_return = DEVICE_MESSAGE_DISPOSITION_RESULT_ABANDONED;

	TEST_messenger_subscribe_for_messages_saved_messenger_handle = NULL;
	TEST_messenger_subscribe_for_messages_saved_on_message_received_callback = NULL;
	TEST_messenger_subscribe_for_messages_saved_context = NULL;
	TEST_messenger_subscribe_for_messages_return = 0;

	TEST_authentication_create_saved_on_authentication_changed_callback = NULL;
	TEST_authentication_create_saved_on_authentication_changed_context = NULL;
	TEST_authentication_create_saved_on_error_callback = NULL;
	TEST_authentication_create_saved_on_error_context = NULL;
	TEST_authentication_create_return = TEST_AUTHENTICATION_HANDLE;

	TEST_messenger_create_saved_on_state_changed_callback = NULL;
	TEST_messenger_create_saved_on_state_changed_context = NULL;
	TEST_messenger_create_return = TEST_MESSENGER_HANDLE;

	TEST_messenger_send_async_saved_message = NULL;
	TEST_messenger_send_async_saved_callback = NULL;
	TEST_messenger_send_async_saved_context = NULL;
	TEST_messenger_send_async_result = 0;

	TEST_on_device_d2c_event_send_complete_callback_saved_message = NULL;
	TEST_on_device_d2c_event_send_complete_callback_saved_result = D2C_EVENT_SEND_COMPLETE_RESULT_ERROR_UNKNOWN;
	TEST_on_device_d2c_event_send_complete_callback_saved_context = NULL;
}

static void register_umock_alias_types()
{
	REGISTER_UMOCK_ALIAS_TYPE(STRING_HANDLE, void*);
	REGISTER_UMOCK_ALIAS_TYPE(AUTHENTICATION_STATE, int);
	REGISTER_UMOCK_ALIAS_TYPE(const AUTHENTICATION_CONFIG*, void*);
	REGISTER_UMOCK_ALIAS_TYPE(AUTHENTICATION_HANDLE, void*);
	REGISTER_UMOCK_ALIAS_TYPE(MESSENGER_STATE, int);
	REGISTER_UMOCK_ALIAS_TYPE(const MESSENGER_CONFIG*, void*);
	REGISTER_UMOCK_ALIAS_TYPE(MESSENGER_HANDLE, void*);
	REGISTER_UMOCK_ALIAS_TYPE(CBS_HANDLE, void*);
	REGISTER_UMOCK_ALIAS_TYPE(const CBS_HANDLE, void*);
	REGISTER_UMOCK_ALIAS_TYPE(SESSION_HANDLE, void*);
	REGISTER_UMOCK_ALIAS_TYPE(const SESSION_HANDLE, void*);
	REGISTER_UMOCK_ALIAS_TYPE(time_t, long long);
	REGISTER_UMOCK_ALIAS_TYPE(AUTHENTICATION_ERROR_CODE, int);
	REGISTER_UMOCK_ALIAS_TYPE(OPTIONHANDLER_HANDLE, void*);
	REGISTER_UMOCK_ALIAS_TYPE(OPTIONHANDLER_RESULT, int);
	REGISTER_UMOCK_ALIAS_TYPE(pfCloneOption, void*);
	REGISTER_UMOCK_ALIAS_TYPE(pfDestroyOption, void*);
	REGISTER_UMOCK_ALIAS_TYPE(pfSetOption, void*);
	REGISTER_UMOCK_ALIAS_TYPE(ON_MESSENGER_MESSAGE_RECEIVED, void*);
	REGISTER_UMOCK_ALIAS_TYPE(ON_MESSENGER_EVENT_SEND_COMPLETE, int);
}

static void register_global_mock_hooks()
{
	REGISTER_GLOBAL_MOCK_HOOK(malloc, TEST_malloc);
	REGISTER_GLOBAL_MOCK_HOOK(free, TEST_free);
	REGISTER_GLOBAL_MOCK_HOOK(mallocAndStrcpy_s, TEST_mallocAndStrcpy_s);
	REGISTER_GLOBAL_MOCK_HOOK(messenger_subscribe_for_messages, TEST_messenger_subscribe_for_messages);
	REGISTER_GLOBAL_MOCK_HOOK(get_difftime, TEST_get_difftime);
	REGISTER_GLOBAL_MOCK_HOOK(authentication_create, TEST_authentication_create);
	REGISTER_GLOBAL_MOCK_HOOK(messenger_create, TEST_messenger_create);
	REGISTER_GLOBAL_MOCK_HOOK(messenger_send_async, TEST_messenger_send_async);
}

static void register_global_mock_returns()
{
	REGISTER_GLOBAL_MOCK_RETURN(STRING_construct, TEST_STRING_HANDLE);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(STRING_construct, NULL);

	REGISTER_GLOBAL_MOCK_RETURN(STRING_c_str, TEST_GENERIC_CHAR_PTR);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(STRING_c_str, NULL);

	REGISTER_GLOBAL_MOCK_FAIL_RETURN(STRING_new, NULL);

	REGISTER_GLOBAL_MOCK_FAIL_RETURN(malloc, NULL);

	REGISTER_GLOBAL_MOCK_FAIL_RETURN(get_time, INDEFINITE_TIME);

	REGISTER_GLOBAL_MOCK_FAIL_RETURN(OptionHandler_Create, NULL);

	REGISTER_GLOBAL_MOCK_RETURN(OptionHandler_AddOption, OPTIONHANDLER_OK);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(OptionHandler_AddOption, OPTIONHANDLER_ERROR);

	REGISTER_GLOBAL_MOCK_RETURN(OptionHandler_FeedOptions, OPTIONHANDLER_OK);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(OptionHandler_FeedOptions, OPTIONHANDLER_ERROR);

	REGISTER_GLOBAL_MOCK_RETURN(authentication_create, TEST_AUTHENTICATION_HANDLE);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(authentication_create, NULL);

	REGISTER_GLOBAL_MOCK_RETURN(authentication_stop, 0);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(authentication_stop, 1);

	REGISTER_GLOBAL_MOCK_RETURN(authentication_set_option, 0);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(authentication_set_option, 1);

	REGISTER_GLOBAL_MOCK_RETURN(messenger_create, TEST_MESSENGER_HANDLE);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(messenger_create, NULL);

	REGISTER_GLOBAL_MOCK_RETURN(messenger_stop, 0);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(messenger_stop, 1);

	REGISTER_GLOBAL_MOCK_RETURN(messenger_set_option, 0);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(messenger_set_option, 1);

	REGISTER_GLOBAL_MOCK_RETURN(messenger_send_async, 0);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(messenger_send_async, 1);

	REGISTER_GLOBAL_MOCK_RETURN(mallocAndStrcpy_s, 0);
	REGISTER_GLOBAL_MOCK_FAIL_RETURN(mallocAndStrcpy_s, 1);
}



// ---------- Expected Call Helpers ---------- //

static void set_expected_calls_for_is_timeout_reached(time_t current_time)
{
	STRICT_EXPECTED_CALL(get_time(NULL)).SetReturn(current_time);

	if (current_time != INDEFINITE_TIME)
	{
		STRICT_EXPECTED_CALL(get_difftime(current_time, IGNORED_NUM_ARG))
			.IgnoreArgument(2); // let the function hook calculate the actual difftime.
	}
}

static void set_expected_calls_for_clone_device_config(DEVICE_CONFIG *config)
{
	EXPECTED_CALL(malloc(IGNORED_NUM_ARG));
	STRICT_EXPECTED_CALL(mallocAndStrcpy_s(IGNORED_PTR_ARG, config->device_id))
		.IgnoreArgument(1);
	STRICT_EXPECTED_CALL(mallocAndStrcpy_s(IGNORED_PTR_ARG, config->iothub_host_fqdn))
		.IgnoreArgument(1);

	if (config->device_sas_token != NULL)
	{
		STRICT_EXPECTED_CALL(mallocAndStrcpy_s(IGNORED_PTR_ARG, config->device_sas_token))
			.IgnoreArgument(1);
	}

	if (config->device_primary_key != NULL)
	{
		STRICT_EXPECTED_CALL(mallocAndStrcpy_s(IGNORED_PTR_ARG, config->device_primary_key))
			.IgnoreArgument(1);
	}

	if (config->device_secondary_key != NULL)
	{
		STRICT_EXPECTED_CALL(mallocAndStrcpy_s(IGNORED_PTR_ARG, config->device_secondary_key))
			.IgnoreArgument(1);
	}
}

static void set_expected_calls_for_create_authentication_instance(DEVICE_CONFIG *config)
{
	(void)config;
	EXPECTED_CALL(authentication_create(IGNORED_PTR_ARG));
}

static void set_expected_calls_for_create_messenger_instance(DEVICE_CONFIG *config)
{
	(void)config;
	EXPECTED_CALL(messenger_create(IGNORED_PTR_ARG));
}

static void set_expected_calls_for_device_create(DEVICE_CONFIG *config, time_t current_time)
{
	(void)current_time;

	EXPECTED_CALL(malloc(IGNORED_NUM_ARG));

	set_expected_calls_for_clone_device_config(config);

	if (config->authentication_mode == DEVICE_AUTH_MODE_CBS)
	{
		set_expected_calls_for_create_authentication_instance(config);
	}

	set_expected_calls_for_create_messenger_instance(config);
}

static void set_expected_calls_for_device_start_async(DEVICE_CONFIG* config, time_t current_time)
{
	(void)config;
	(void)current_time;
	// Nothing to expect from this function.
}

static void set_expected_calls_for_device_stop(DEVICE_CONFIG* config, time_t current_time, AUTHENTICATION_STATE auth_state, MESSENGER_STATE messenger_state)
{
	(void)current_time;

	if (messenger_state != MESSENGER_STATE_STOPPED && messenger_state != MESSENGER_STATE_STOPPING)
	{
		STRICT_EXPECTED_CALL(messenger_stop(TEST_MESSENGER_HANDLE)).SetReturn(0);
	}

	if (config->authentication_mode == DEVICE_AUTH_MODE_CBS && auth_state != AUTHENTICATION_STATE_STOPPED)
	{
		STRICT_EXPECTED_CALL(authentication_stop(TEST_AUTHENTICATION_HANDLE)).SetReturn(0);
	}
}

static void set_expected_calls_for_device_do_work(DEVICE_CONFIG* config, time_t current_time, DEVICE_STATE device_state, AUTHENTICATION_STATE auth_state, MESSENGER_STATE msgr_state)
{
	if (device_state == DEVICE_STATE_STARTING)
	{
		if (config->authentication_mode == DEVICE_AUTH_MODE_CBS)
		{
			if (auth_state == AUTHENTICATION_STATE_STOPPED)
			{
				STRICT_EXPECTED_CALL(authentication_start(TEST_AUTHENTICATION_HANDLE, TEST_CBS_HANDLE)).SetReturn(0);
			}
			else if (auth_state == AUTHENTICATION_STATE_STARTING)
			{
				set_expected_calls_for_is_timeout_reached(current_time);
			}
		}

		if (config->authentication_mode == DEVICE_AUTH_MODE_X509 || auth_state == AUTHENTICATION_STATE_STARTED)
		{
			if (msgr_state == MESSENGER_STATE_STOPPED)
			{
				STRICT_EXPECTED_CALL(messenger_start(TEST_MESSENGER_HANDLE, TEST_SESSION_HANDLE)).SetReturn(0);
			}
			else if (msgr_state == MESSENGER_STATE_STARTING)
			{
				set_expected_calls_for_is_timeout_reached(current_time);
			}
		}
	}

	if (config->authentication_mode == DEVICE_AUTH_MODE_CBS)
	{
		if (auth_state != AUTHENTICATION_STATE_STOPPED && auth_state != AUTHENTICATION_STATE_ERROR)
		{
			STRICT_EXPECTED_CALL(authentication_do_work(TEST_AUTHENTICATION_HANDLE));
		}
	}

	if (msgr_state != MESSENGER_STATE_STOPPED && msgr_state != MESSENGER_STATE_ERROR)
	{
		STRICT_EXPECTED_CALL(messenger_do_work(TEST_MESSENGER_HANDLE));
	}
}

static void set_expected_calls_for_device_destroy(DEVICE_HANDLE handle, DEVICE_CONFIG *config, time_t current_time, DEVICE_STATE device_state, AUTHENTICATION_STATE auth_state, MESSENGER_STATE msgr_state)
{
	if (device_state == DEVICE_STATE_STARTED || device_state == DEVICE_STATE_STARTING)
	{
		set_expected_calls_for_device_stop(config, current_time, auth_state, msgr_state);
	}

	if (msgr_state != DEVICE_STATE_STOPPED)
	{
		STRICT_EXPECTED_CALL(messenger_destroy(TEST_MESSENGER_HANDLE));
	}

	if (config->authentication_mode == DEVICE_AUTH_MODE_CBS && auth_state != AUTHENTICATION_STATE_STOPPED)
	{
		STRICT_EXPECTED_CALL(authentication_destroy(TEST_AUTHENTICATION_HANDLE));
	}

	// destroy config
	STRICT_EXPECTED_CALL(free(config->device_id));
	STRICT_EXPECTED_CALL(free(config->iothub_host_fqdn));
	STRICT_EXPECTED_CALL(free(config->device_primary_key));
	STRICT_EXPECTED_CALL(free(config->device_secondary_key));
	STRICT_EXPECTED_CALL(free(config->device_sas_token));
	EXPECTED_CALL(free(IGNORED_PTR_ARG));

	STRICT_EXPECTED_CALL(free(handle));
}

static void set_expected_calls_for_device_retrieve_options(DEVICE_CONFIG *config)
{
	EXPECTED_CALL(OptionHandler_Create(IGNORED_PTR_ARG, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.SetReturn(TEST_OPTIONHANDLER_HANDLE);

	if (config->authentication_mode == DEVICE_AUTH_MODE_CBS)
	{
		STRICT_EXPECTED_CALL(authentication_retrieve_options(TEST_AUTHENTICATION_HANDLE))
			.SetReturn(TEST_AUTH_OPTIONHANDLER_HANDLE);
		STRICT_EXPECTED_CALL(OptionHandler_AddOption(TEST_OPTIONHANDLER_HANDLE, DEVICE_OPTION_SAVED_AUTH_OPTIONS, TEST_AUTH_OPTIONHANDLER_HANDLE))
			.SetReturn(OPTIONHANDLER_OK);
	}

	STRICT_EXPECTED_CALL(messenger_retrieve_options(TEST_MESSENGER_HANDLE))
		.SetReturn(TEST_MSGR_OPTIONHANDLER_HANDLE);
	STRICT_EXPECTED_CALL(OptionHandler_AddOption(TEST_OPTIONHANDLER_HANDLE, DEVICE_OPTION_SAVED_MESSENGER_OPTIONS, TEST_MSGR_OPTIONHANDLER_HANDLE))
		.SetReturn(OPTIONHANDLER_OK);
}

static void set_expected_calls_for_device_set_option(DEVICE_HANDLE handle, DEVICE_CONFIG *config, const char* option_name, void* option_value)
{
	if (config->authentication_mode == DEVICE_AUTH_MODE_CBS)
	{
		if (strcmp(DEVICE_OPTION_CBS_REQUEST_TIMEOUT_SECS, option_name) == 0 ||
			strcmp(DEVICE_OPTION_SAS_TOKEN_REFRESH_TIME_SECS, option_name) == 0 ||
			strcmp(DEVICE_OPTION_SAS_TOKEN_LIFETIME_SECS, option_name) == 0)
		{
			STRICT_EXPECTED_CALL(authentication_set_option(TEST_AUTHENTICATION_HANDLE, option_name, option_value));
		}
		else if (strcmp(DEVICE_OPTION_SAVED_AUTH_OPTIONS, option_name) == 0)
		{
			STRICT_EXPECTED_CALL(OptionHandler_FeedOptions((OPTIONHANDLER_HANDLE)option_value, TEST_AUTHENTICATION_HANDLE));
		}
	}

	if (strcmp(DEVICE_OPTION_EVENT_SEND_TIMEOUT_SECS, option_name) == 0)
	{
		STRICT_EXPECTED_CALL(messenger_set_option(TEST_MESSENGER_HANDLE, option_name, option_value));
	}
	else if (strcmp(DEVICE_OPTION_SAVED_MESSENGER_OPTIONS, option_name) == 0)
	{
		STRICT_EXPECTED_CALL(OptionHandler_FeedOptions((OPTIONHANDLER_HANDLE)option_value, TEST_MESSENGER_HANDLE));
	}
	else if (strcmp(DEVICE_OPTION_SAVED_OPTIONS, option_name) == 0)
	{
		STRICT_EXPECTED_CALL(OptionHandler_FeedOptions((OPTIONHANDLER_HANDLE)option_value, handle));
	}
}

static void set_expected_calls_for_device_send_async(DEVICE_CONFIG *config)
{
	(void)config;
	EXPECTED_CALL(malloc(IGNORED_NUM_ARG));
	STRICT_EXPECTED_CALL(messenger_send_async(TEST_MESSENGER_HANDLE, TEST_IOTHUB_MESSAGE_LIST, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.IgnoreArgument(3)
		.IgnoreArgument(4);
}


// ---------- set_expected*-dependent Test Helpers ---------- //

static DEVICE_HANDLE create_device(DEVICE_CONFIG* config, time_t current_time)
{
	umock_c_reset_all_calls();
	set_expected_calls_for_device_create(config, current_time);
	return device_create(config);
}

static DEVICE_HANDLE create_and_start_device(DEVICE_CONFIG* config, time_t current_time)
{
	DEVICE_HANDLE handle = create_device(config, current_time);

	set_expected_calls_for_device_start_async(config, current_time);

	if (config->authentication_mode == DEVICE_AUTH_MODE_CBS)
	{
		(void)device_start_async(handle, TEST_SESSION_HANDLE, TEST_CBS_HANDLE);
	}
	else
	{
		(void)device_start_async(handle, TEST_SESSION_HANDLE, NULL);
	}

	return handle;
}

static void crank_device_do_work(DEVICE_HANDLE handle, DEVICE_CONFIG* config, time_t current_time, DEVICE_STATE device_state, AUTHENTICATION_STATE auth_state, MESSENGER_STATE msgr_state)
{
	umock_c_reset_all_calls();
	set_expected_calls_for_device_do_work(config, current_time, device_state, auth_state, msgr_state);
	device_do_work(handle);
}

static void set_authentication_state(AUTHENTICATION_STATE previous_state, AUTHENTICATION_STATE new_state, time_t current_time)
{
	STRICT_EXPECTED_CALL(get_time(NULL)).SetReturn(current_time);

	TEST_authentication_create_saved_on_authentication_changed_callback(
		TEST_authentication_create_saved_on_authentication_changed_context, 
		previous_state, 
		new_state);
}

static void set_messenger_state(MESSENGER_STATE previous_state, MESSENGER_STATE new_state, time_t current_time)
{
	STRICT_EXPECTED_CALL(get_time(NULL)).SetReturn(current_time);

	TEST_messenger_create_saved_on_state_changed_callback(
		TEST_messenger_create_saved_on_state_changed_context, 
		previous_state, 
		new_state);
}

static DEVICE_HANDLE create_and_start_and_crank_device(DEVICE_CONFIG* config, time_t current_time)
{
	DEVICE_HANDLE handle = create_and_start_device(config, current_time);

	crank_device_do_work(handle, config, current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STOPPED, MESSENGER_STATE_STOPPED);
	
	set_authentication_state(AUTHENTICATION_STATE_STOPPED, AUTHENTICATION_STATE_STARTING, current_time);
	set_authentication_state(AUTHENTICATION_STATE_STARTING, AUTHENTICATION_STATE_STARTED, current_time);
	
	crank_device_do_work(handle, config, current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_STOPPED);
	
	set_messenger_state(MESSENGER_STATE_STOPPED, MESSENGER_STATE_STARTING, TEST_current_time);
	set_messenger_state(MESSENGER_STATE_STARTING, MESSENGER_STATE_STARTED, TEST_current_time);
	
	crank_device_do_work(handle, config, current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_STARTED);

	return handle;
}


BEGIN_TEST_SUITE(iothubtransport_amqp_device_ut)

TEST_SUITE_INITIALIZE(TestClassInitialize)
{
    TEST_INITIALIZE_MEMORY_DEBUG(g_dllByDll);
    g_testByTest = TEST_MUTEX_CREATE();
    ASSERT_IS_NOT_NULL(g_testByTest);

    umock_c_init(on_umock_c_error);

    int result = umocktypes_charptr_register_types();
    ASSERT_ARE_EQUAL(int, 0, result);
    result = umocktypes_stdint_register_types();
    ASSERT_ARE_EQUAL(int, 0, result);
    result = umocktypes_bool_register_types();
    ASSERT_ARE_EQUAL(int, 0, result);

	register_umock_alias_types();
	register_global_mock_returns();
	register_global_mock_hooks();
}

TEST_SUITE_CLEANUP(TestClassCleanup)
{
    umock_c_deinit();

    TEST_MUTEX_DESTROY(g_testByTest);
    TEST_DEINITIALIZE_MEMORY_DEBUG(g_dllByDll);
}

TEST_FUNCTION_INITIALIZE(TestMethodInitialize)
{
    if (TEST_MUTEX_ACQUIRE(g_testByTest))
    {
        ASSERT_FAIL("our mutex is ABANDONED. Failure in test framework");
    }

    umock_c_reset_all_calls();

	initialize_test_variables();
}

TEST_FUNCTION_CLEANUP(TestMethodCleanup)
{
    TEST_MUTEX_RELEASE(g_testByTest);
}


// Tests_SRS_DEVICE_09_001: [If `config` or device_id or iothub_host_fqdn or on_state_changed_callback are NULL then device_create shall fail and return NULL]
TEST_FUNCTION(device_create_NULL_config)
{
	// arrange

	// act
	DEVICE_HANDLE handle = device_create(NULL);

	// assert
	ASSERT_IS_NULL(handle);

	// cleanup
}

// Tests_SRS_DEVICE_09_001: [If `config` or device_id or iothub_host_fqdn or on_state_changed_callback are NULL then device_create shall fail and return NULL]
TEST_FUNCTION(device_create_NULL_config_device_id)
{
	// arrange
	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	config->device_id = NULL;

	// act
	DEVICE_HANDLE handle = device_create(config);

	// assert
	ASSERT_IS_NULL(handle);

	// cleanup
}

// Tests_SRS_DEVICE_09_001: [If `config` or device_id or iothub_host_fqdn or on_state_changed_callback are NULL then device_create shall fail and return NULL]
TEST_FUNCTION(device_create_NULL_config_iothub_host_fqdn)
{
	// arrange
	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	config->iothub_host_fqdn = NULL;

	// act
	DEVICE_HANDLE handle = device_create(config);

	// assert
	ASSERT_IS_NULL(handle);

	// cleanup
}

// Tests_SRS_DEVICE_09_001: [If `config` or device_id or iothub_host_fqdn or on_state_changed_callback are NULL then device_create shall fail and return NULL]
TEST_FUNCTION(device_create_NULL_config_on_state_changed_callback)
{
	// arrange
	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	config->on_state_changed_callback = NULL;

	// act
	DEVICE_HANDLE handle = device_create(config);

	// assert
	ASSERT_IS_NULL(handle);

	// cleanup
}

// Tests_SRS_DEVICE_09_002: [device_create shall allocate memory for the device instance structure]
// Tests_SRS_DEVICE_09_004: [All `config` parameters shall be saved into `instance`]
// Tests_SRS_DEVICE_09_006: [If `instance->authentication_mode` is DEVICE_AUTH_MODE_CBS, `instance->authentication_handle` shall be set using authentication_create()]
// Tests_SRS_DEVICE_09_008: [`instance->messenger_handle` shall be set using messenger_create()]
// Tests_SRS_DEVICE_09_011: [If device_create succeeds it shall return a handle to its `instance` structure]
TEST_FUNCTION(device_create_succeeds)
{
	// arrange
	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);

	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");
	set_expected_calls_for_device_create(config, TEST_current_time);

	// act
	DEVICE_HANDLE handle = device_create(config);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_003: [If malloc fails, device_create shall fail and return NULL]
// Tests_SRS_DEVICE_09_005: [If any `config` parameters fail to be saved into `instance`, device_create shall fail and return NULL]
// Tests_SRS_DEVICE_09_007: [If the AUTHENTICATION_HANDLE fails to be created, device_create shall fail and return NULL]
// Tests_SRS_DEVICE_09_009: [If the MESSENGER_HANDLE fails to be created, device_create shall fail and return NULL]
// Tests_SRS_DEVICE_09_010: [If device_create fails it shall release all memory it has allocated]
TEST_FUNCTION(device_create_failure_checks)
{
	// arrange
	ASSERT_ARE_EQUAL(int, 0, umock_c_negative_tests_init());
	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");
	
	umock_c_reset_all_calls();
	set_expected_calls_for_device_create(config, TEST_current_time);
	umock_c_negative_tests_snapshot();

	// act
	size_t i;
	for (i = 0; i < umock_c_negative_tests_call_count(); i++)
	{
		// arrange 
		char error_msg[64];

		umock_c_negative_tests_reset();
		umock_c_negative_tests_fail_call(i);

		DEVICE_HANDLE handle = device_create(config);

		// assert
		sprintf(error_msg, "On failed call %zu", i);
		ASSERT_IS_NULL_WITH_MSG(handle, error_msg);
	}
	
	// cleanup
	umock_c_negative_tests_deinit();
	umock_c_reset_all_calls();
}

// Tests_SRS_DEVICE_09_017: [If `handle` is NULL, device_start_async shall return a non-zero result]
TEST_FUNCTION(device_start_async_NULL_handle)
{
	// arrange

	// act
	int result = device_start_async(NULL, TEST_SESSION_HANDLE, TEST_CBS_HANDLE);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
}

// Tests_SRS_DEVICE_09_018: [If the device state is not DEVICE_STATE_STOPPED, device_start_async shall return a non-zero result]
TEST_FUNCTION(device_start_async_device_not_stopped)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STOPPED, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTING, TEST_on_state_changed_callback_saved_new_state);

	// act
	int result = device_start_async(handle, TEST_SESSION_HANDLE, TEST_CBS_HANDLE);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STOPPED, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTING, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_019: [If `session_handle` is NULL, device_start_async shall return a non-zero result]
TEST_FUNCTION(device_start_async_NULL_session_handle)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_start_async(config, TEST_current_time);

	// act
	int result = device_start_async(handle, NULL, TEST_CBS_HANDLE);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_020: [If using CBS authentication and `cbs_handle` is NULL, device_start_async shall return a non-zero result]
TEST_FUNCTION(device_start_async_CBS_NULL_cbs_handle)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_start_async(config, TEST_current_time);

	// act
	int result = device_start_async(handle, TEST_SESSION_HANDLE, NULL);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_022: [The device state shall be updated to DEVICE_STATE_STARTING, and state changed callback invoked]
// Tests_SRS_DEVICE_09_023: [If no failures occur, device_start_async shall return 0]
TEST_FUNCTION(device_start_async_X509_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_X509, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_start_async(config, TEST_current_time);

	// act
	int result = device_start_async(handle, TEST_SESSION_HANDLE, NULL);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STOPPED, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTING, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_022: [The device state shall be updated to DEVICE_STATE_STARTING, and state changed callback invoked]
// Tests_SRS_DEVICE_09_023: [If no failures occur, device_start_async shall return 0]
TEST_FUNCTION(device_start_async_CBS_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_start_async(config, TEST_current_time);

	// act
	int result = device_start_async(handle, TEST_SESSION_HANDLE, TEST_CBS_HANDLE);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STOPPED, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTING, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}


// Tests_SRS_DEVICE_09_024: [If `handle` is NULL, device_stop shall return a non-zero result]
TEST_FUNCTION(device_stop_NULL_handle)
{
	// arrange

	// act
	int result = device_stop(NULL);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
}

// Tests_SRS_DEVICE_09_025: [If the device state is already DEVICE_STATE_STOPPED or DEVICE_STATE_STOPPING, device_stop shall return a non-zero result]
TEST_FUNCTION(device_stop_device_already_stopped)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_stop(config, TEST_current_time, AUTHENTICATION_STATE_STOPPED, MESSENGER_STATE_STOPPED);

	// act
	int result = device_stop(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STOPPED, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STOPPED, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_028: [If messenger_stop fails, the `instance` state shall be updated to DEVICE_STATE_ERROR_MSG and the function shall return non-zero result]
// Tests_SRS_DEVICE_09_030: [If authentication_stop fails, the `instance` state shall be updated to DEVICE_STATE_ERROR_AUTH and the function shall return non-zero result]
TEST_FUNCTION(device_stop_DEVICE_STATE_STARTED_failure_checks)
{
	// arrange
	ASSERT_ARE_EQUAL(int, 0, umock_c_negative_tests_init());
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);

	size_t i, n;
	for (i = 0, n = 1; i < n; i++)
	{
		// arrange 
		DEVICE_HANDLE handle = create_and_start_and_crank_device(config, TEST_current_time);

		umock_c_reset_all_calls();
		set_expected_calls_for_device_stop(config, TEST_current_time, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_STARTED);
		umock_c_negative_tests_snapshot();
		n = umock_c_negative_tests_call_count();

		char error_msg[64];

		umock_c_negative_tests_reset();
		umock_c_negative_tests_fail_call(i);

		int result = device_stop(handle);

		// assert
		sprintf(error_msg, "On failed call %zu", i);
	
		ASSERT_ARE_NOT_EQUAL_WITH_MSG(int, 0, result, error_msg);
		ASSERT_ARE_EQUAL_WITH_MSG(int, DEVICE_STATE_STOPPING, TEST_on_state_changed_callback_saved_previous_state, error_msg);
		ASSERT_IS_TRUE_WITH_MSG(DEVICE_STATE_ERROR_AUTH == TEST_on_state_changed_callback_saved_new_state || DEVICE_STATE_ERROR_MSG == TEST_on_state_changed_callback_saved_new_state, error_msg);
		ASSERT_IS_NOT_NULL_WITH_MSG(handle, error_msg);

		// cleanup
		device_destroy(handle);
	}

	// cleanup
	umock_c_negative_tests_deinit();
	umock_c_reset_all_calls();
}


// Tests_SRS_DEVICE_09_026: [The device state shall be updated to DEVICE_STATE_STOPPING, and state changed callback invoked]
// Tests_SRS_DEVICE_09_031: [The device state shall be updated to DEVICE_STATE_STOPPED, and state changed callback invoked]
// Tests_SRS_DEVICE_09_032: [If no failures occur, device_stop shall return 0]
TEST_FUNCTION(device_stop_DEVICE_STATE_STARTING_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_stop(config, TEST_current_time, AUTHENTICATION_STATE_STOPPED, MESSENGER_STATE_STOPPED);

	// act
	int result = device_stop(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STOPPING, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STOPPED, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_027: [If `instance->messenger_handle` state is not MESSENGER_STATE_STOPPED, messenger_stop shall be invoked]
// Tests_SRS_DEVICE_09_029: [If CBS authentication is used, if `instance->authentication_handle` state is not AUTHENTICATION_STATE_STOPPED, authentication_stop shall be invoked]
TEST_FUNCTION(device_stop_DEVICE_STATE_STARTED_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_and_crank_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_stop(config, TEST_current_time, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_STARTED);

	// act
	int result = device_stop(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STOPPING, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STOPPED, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_012: [If `handle` is NULL, device_destroy shall return]
TEST_FUNCTION(device_destroy_NULL_handle)
{
	// arrange
	umock_c_reset_all_calls();

	// act
	device_destroy(NULL);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

	// cleanup
}

// Tests_SRS_DEVICE_09_013: [If the device is in state DEVICE_STATE_STARTED or DEVICE_STATE_STARTING, device_stop() shall be invoked]
// Tests_SRS_DEVICE_09_014: [`instance->messenger_handle shall be destroyed using messenger_destroy()`]
// Tests_SRS_DEVICE_09_015: [If created, `instance->authentication_handle` shall be destroyed using authentication_destroy()`]
// Tests_SRS_DEVICE_09_016: [The contents of `instance->config` shall be detroyed and then it shall be freed]
TEST_FUNCTION(device_destroy_DEVICE_STATE_STARTED_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_and_crank_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_destroy(handle, config, TEST_current_time, DEVICE_STATE_STARTED, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_STARTED);

	// act
	device_destroy(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
}


// Tests_SRS_DEVICE_09_105: [If `handle` or `send_status` is NULL, device_get_send_status shall return a non-zero result]
TEST_FUNCTION(device_get_send_status_NULL_handle)
{
	// arrange
	umock_c_reset_all_calls();

	DEVICE_SEND_STATUS send_status;

	// act
	int result = device_get_send_status(NULL, &send_status);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);

	// cleanup
}

// Tests_SRS_DEVICE_09_105: [If `handle` or `send_status` is NULL, device_get_send_status shall return a non-zero result]
TEST_FUNCTION(device_get_send_status_NULL_send_status)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();

	// act
	int result = device_get_send_status(handle, NULL);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_106: [The status of `instance->messenger_handle` shall be obtained using messenger_get_send_status]
// Tests_SRS_DEVICE_09_108: [If messenger_get_send_status returns MESSENGER_SEND_STATUS_IDLE, device_get_send_status return status DEVICE_SEND_STATUS_IDLE]
// Tests_SRS_DEVICE_09_110: [If device_get_send_status succeeds, it shall return zero as result]
TEST_FUNCTION(device_get_send_status_IDLE_success)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	MESSENGER_SEND_STATUS messenger_get_send_status_result = MESSENGER_SEND_STATUS_IDLE;

	umock_c_reset_all_calls();
	STRICT_EXPECTED_CALL(messenger_get_send_status(TEST_MESSENGER_HANDLE, IGNORED_PTR_ARG))
		.IgnoreArgument(2)
		.CopyOutArgumentBuffer(2, &messenger_get_send_status_result, sizeof(MESSENGER_SEND_STATUS))
		.SetReturn(0);

	// act
	DEVICE_SEND_STATUS send_status;
	int result = device_get_send_status(handle, &send_status);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_ARE_EQUAL(int, DEVICE_SEND_STATUS_IDLE, send_status);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_109: [If messenger_get_send_status returns MESSENGER_SEND_STATUS_BUSY, device_get_send_status return status DEVICE_SEND_STATUS_BUSY]
TEST_FUNCTION(device_get_send_status_BUSY_success)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	MESSENGER_SEND_STATUS messenger_get_send_status_result = MESSENGER_SEND_STATUS_BUSY;

	umock_c_reset_all_calls();
	STRICT_EXPECTED_CALL(messenger_get_send_status(TEST_MESSENGER_HANDLE, IGNORED_PTR_ARG))
		.IgnoreArgument(2)
		.CopyOutArgumentBuffer(2, &messenger_get_send_status_result, sizeof(MESSENGER_SEND_STATUS))
		.SetReturn(0);

	// act
	DEVICE_SEND_STATUS send_status;
	int result = device_get_send_status(handle, &send_status);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_ARE_EQUAL(int, DEVICE_SEND_STATUS_BUSY, send_status);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_107: [If messenger_get_send_status fails, device_get_send_status shall return a non-zero result]
TEST_FUNCTION(device_get_send_status_failure_checks)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	STRICT_EXPECTED_CALL(messenger_get_send_status(TEST_MESSENGER_HANDLE, IGNORED_PTR_ARG))
		.IgnoreArgument(2)
		.SetReturn(1);

	// act
	DEVICE_SEND_STATUS send_status;
	int result = device_get_send_status(handle, &send_status);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_066: [If `handle` or `on_message_received_callback` or `context` is NULL, device_subscribe_message shall return a non-zero result]
TEST_FUNCTION(device_subscribe_message_NULL_handle)
{
	// arrange
	umock_c_reset_all_calls();

	// act
	int result = device_subscribe_message(NULL, TEST_on_message_received, TEST_VOID_PTR);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);

	// cleanup
}

// Tests_SRS_DEVICE_09_066: [If `handle` or `on_message_received_callback` or `context` is NULL, device_subscribe_message shall return a non-zero result]
TEST_FUNCTION(device_subscribe_message_NULL_callback)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();

	// act
	int result = device_subscribe_message(handle, NULL, handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_066: [If `handle` or `on_message_received_callback` or `context` is NULL, device_subscribe_message shall return a non-zero result]
TEST_FUNCTION(device_subscribe_message_NULL_context)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();

	// act
	int result = device_subscribe_message(handle, TEST_on_message_received, NULL);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_067: [messenger_subscribe_for_messages shall be invoked passing `on_messenger_message_received_callback` and the user callback and context]
// Tests_SRS_DEVICE_09_069: [If no failures occur, device_subscribe_message shall return 0]
TEST_FUNCTION(device_subscribe_message_succeess)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();

	TEST_messenger_subscribe_for_messages_return = 0;
	STRICT_EXPECTED_CALL(messenger_subscribe_for_messages(TEST_MESSENGER_HANDLE, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.IgnoreArgument(2)
		.IgnoreArgument(3);

	// act
	int result = device_subscribe_message(handle, TEST_on_message_received, handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(TEST_messenger_subscribe_for_messages_saved_on_message_received_callback);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_068: [If messenger_subscribe_for_messages fails, device_subscribe_message shall return a non-zero result]
TEST_FUNCTION(device_subscribe_message_failure_checks)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();

	STRICT_EXPECTED_CALL(messenger_subscribe_for_messages(TEST_MESSENGER_HANDLE, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.IgnoreArgument(2)
		.IgnoreArgument(3)
		.SetReturn(1);

	// act
	int result = device_subscribe_message(handle, TEST_on_message_received, handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_080: [If `handle` is NULL, device_set_retry_policy shall return a non-zero result]
TEST_FUNCTION(device_set_retry_policy_NULL_handle)
{
	// arrange
	umock_c_reset_all_calls();

	// act
	int result = device_set_retry_policy(NULL, IOTHUB_CLIENT_RETRY_EXPONENTIAL_BACKOFF_WITH_JITTER, 300);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);

	// cleanup
}

// Tests_SRS_DEVICE_09_081: [device_set_retry_policy shall return a non-zero result]
TEST_FUNCTION(device_set_retry_policy_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();

	// act
	int result = device_set_retry_policy(handle, IOTHUB_CLIENT_RETRY_EXPONENTIAL_BACKOFF_WITH_JITTER, 300);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_093: [If `handle` is NULL, device_retrieve_options shall return NULL]
TEST_FUNCTION(device_retrieve_options_NULL_handle)
{
	// arrange
	umock_c_reset_all_calls();

	// act
	OPTIONHANDLER_HANDLE options = device_retrieve_options(NULL);

	// assert
	ASSERT_IS_NULL(options);

	// cleanup
}

// Tests_SRS_DEVICE_09_094: [A OPTIONHANDLER_HANDLE instance, aka `options` shall be created using OptionHandler_Create]
// Tests_SRS_DEVICE_09_096: [If CBS authentication is used, `instance->authentication_handle` options shall be retrieved using authentication_retrieve_options]
// Tests_SRS_DEVICE_09_098: [The authentication options shall be added to `options` using OptionHandler_AddOption as DEVICE_OPTION_SAVED_AUTH_OPTIONS]
// Tests_SRS_DEVICE_09_099: [`instance->messenger_handle` options shall be retrieved using messenger_retrieve_options]
// Tests_SRS_DEVICE_09_101: [The messenger options shall be added to `options` using OptionHandler_AddOption as DEVICE_OPTION_SAVED_MESSENGER_OPTIONS]
// Tests_SRS_DEVICE_09_104: [If no failures occur, a handle to `options` shall be return]
TEST_FUNCTION(device_retrieve_options_CBS_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_retrieve_options(config);

	// act
	OPTIONHANDLER_HANDLE result = device_retrieve_options(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(void_ptr, TEST_OPTIONHANDLER_HANDLE, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_096: [If CBS authentication is used, `instance->authentication_handle` options shall be retrieved using authentication_retrieve_options]
TEST_FUNCTION(device_retrieve_options_X509_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_X509, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_retrieve_options(config);

	// act
	OPTIONHANDLER_HANDLE result = device_retrieve_options(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(void_ptr, TEST_OPTIONHANDLER_HANDLE, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_095: [If OptionHandler_Create fails, device_retrieve_options shall return NULL]
// Tests_SRS_DEVICE_09_097: [If authentication_retrieve_options fails, device_retrieve_options shall return NULL]
// Tests_SRS_DEVICE_09_100: [If messenger_retrieve_options fails, device_retrieve_options shall return NULL]
// Tests_SRS_DEVICE_09_102: [If any call to OptionHandler_AddOption fails, device_retrieve_options shall return NULL]
// Tests_SRS_DEVICE_09_103: [If any failure occurs, any memory allocated by device_retrieve_options shall be destroyed]
TEST_FUNCTION(device_retrieve_options_CBS_failure_checks)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");
	
	ASSERT_ARE_EQUAL(int, 0, umock_c_negative_tests_init());
	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_retrieve_options(config);
	umock_c_negative_tests_snapshot();

	// act
	size_t i;
	for (i = 0; i < umock_c_negative_tests_call_count(); i++)
	{
		// arrange 
		char error_msg[64];

		umock_c_negative_tests_reset();
		umock_c_negative_tests_fail_call(i);

		OPTIONHANDLER_HANDLE result = device_retrieve_options(handle);

		// assert
		sprintf(error_msg, "On failed call %zu", i);
		ASSERT_IS_NULL_WITH_MSG(result, error_msg);
	}

	// cleanup
	umock_c_negative_tests_deinit();
	umock_c_reset_all_calls();
	device_destroy(handle);
}


// Tests_SRS_DEVICE_09_082: [If `handle` or `name` or `value` are NULL, device_set_option shall return a non-zero result]


// Tests_SRS_DEVICE_09_083: [If `name` refers to authentication but CBS authentication is not used, device_set_option shall return a non-zero result]
TEST_FUNCTION(device_set_option_X509_AUTH_fails)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_X509, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	size_t value = 400;

	umock_c_reset_all_calls();
	set_expected_calls_for_device_set_option(handle, config, DEVICE_OPTION_CBS_REQUEST_TIMEOUT_SECS, &value);

	// act
	int result = device_set_option(handle, DEVICE_OPTION_CBS_REQUEST_TIMEOUT_SECS, &value);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_085: [If authentication_set_option fails, device_set_option shall return a non-zero result]
TEST_FUNCTION(device_set_option_saved_auth_options_fails)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	ASSERT_ARE_EQUAL(int, 0, umock_c_negative_tests_init());

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	size_t value = 400;

	umock_c_reset_all_calls();
	set_expected_calls_for_device_set_option(handle, config, DEVICE_OPTION_CBS_REQUEST_TIMEOUT_SECS, &value);
	umock_c_negative_tests_snapshot();

	umock_c_negative_tests_reset();
	umock_c_negative_tests_fail_call(0);

	// act
	int result = device_set_option(handle, DEVICE_OPTION_CBS_REQUEST_TIMEOUT_SECS, &value);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	umock_c_negative_tests_deinit();
	umock_c_reset_all_calls();

	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_087: [If messenger_set_option fails, device_set_option shall return a non-zero result]
TEST_FUNCTION(device_set_option_saved_msgr_options_fails)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	ASSERT_ARE_EQUAL(int, 0, umock_c_negative_tests_init());

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	size_t value = 400;

	umock_c_reset_all_calls();
	set_expected_calls_for_device_set_option(handle, config, DEVICE_OPTION_EVENT_SEND_TIMEOUT_SECS, &value);
	umock_c_negative_tests_snapshot();

	umock_c_negative_tests_reset();
	umock_c_negative_tests_fail_call(0);

	// act
	int result = device_set_option(handle, DEVICE_OPTION_EVENT_SEND_TIMEOUT_SECS, &value);

	// assert
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	umock_c_negative_tests_deinit();
	umock_c_reset_all_calls();

	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_084: [If `name` refers to authentication, it shall be passed along with `value` to authentication_set_option]
// Tests_SRS_DEVICE_09_092: [If no failures occur, device_set_option shall return 0]
TEST_FUNCTION(device_set_option_CBS_AUTH_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	size_t value = 400;

	umock_c_reset_all_calls();
	set_expected_calls_for_device_set_option(handle, config, DEVICE_OPTION_CBS_REQUEST_TIMEOUT_SECS, &value);

	// act
	int result = device_set_option(handle, DEVICE_OPTION_CBS_REQUEST_TIMEOUT_SECS, &value);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_086: [If `name` refers to messenger module, it shall be passed along with `value` to messenger_set_option]
// Tests_SRS_DEVICE_09_092: [If no failures occur, device_set_option shall return 0]
TEST_FUNCTION(device_set_option_MSGR_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	size_t value = 400;

	umock_c_reset_all_calls();
	set_expected_calls_for_device_set_option(handle, config, DEVICE_OPTION_EVENT_SEND_TIMEOUT_SECS, &value);

	// act
	int result = device_set_option(handle, DEVICE_OPTION_EVENT_SEND_TIMEOUT_SECS, &value);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_088: [If `name` is DEVICE_OPTION_SAVED_AUTH_OPTIONS but CBS authentication is not being used, device_set_option shall return a non-zero result]
TEST_FUNCTION(device_set_option_X509_saved_auth_options)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_X509, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	OPTIONHANDLER_HANDLE value = TEST_AUTH_OPTIONHANDLER_HANDLE;

	umock_c_reset_all_calls();
	set_expected_calls_for_device_set_option(handle, config, DEVICE_OPTION_SAVED_AUTH_OPTIONS, &value); // no-op

	// act
	int result = device_set_option(handle, DEVICE_OPTION_SAVED_AUTH_OPTIONS, &value);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

TEST_FUNCTION(device_set_option_AUTH_saved_auth_options_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	OPTIONHANDLER_HANDLE value = TEST_AUTH_OPTIONHANDLER_HANDLE;

	umock_c_reset_all_calls();
	set_expected_calls_for_device_set_option(handle, config, DEVICE_OPTION_SAVED_AUTH_OPTIONS, &value);

	// act
	int result = device_set_option(handle, DEVICE_OPTION_SAVED_AUTH_OPTIONS, &value);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_089: [If `name` is DEVICE_OPTION_SAVED_MESSENGER_OPTIONS, `value` shall be fed to `instance->messenger_handle` using OptionHandler_FeedOptions]
TEST_FUNCTION(device_set_option_MSGR_saved_msgr_options_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	OPTIONHANDLER_HANDLE value = TEST_MSGR_OPTIONHANDLER_HANDLE;

	umock_c_reset_all_calls();
	set_expected_calls_for_device_set_option(handle, config, DEVICE_OPTION_SAVED_MESSENGER_OPTIONS, &value);

	// act
	int result = device_set_option(handle, DEVICE_OPTION_SAVED_MESSENGER_OPTIONS, &value);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_090: [If `name` is DEVICE_OPTION_SAVED_OPTIONS, `value` shall be fed to `instance` using OptionHandler_FeedOptions]
TEST_FUNCTION(device_set_option_saved_device_options_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	OPTIONHANDLER_HANDLE value = TEST_OPTIONHANDLER_HANDLE;

	umock_c_reset_all_calls();
	set_expected_calls_for_device_set_option(handle, config, DEVICE_OPTION_SAVED_OPTIONS, &value);

	// act
	int result = device_set_option(handle, DEVICE_OPTION_SAVED_OPTIONS, &value);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_091: [If any call to OptionHandler_FeedOptions fails, device_set_option shall return a non-zero result]
TEST_FUNCTION(device_set_option_saved_device_options_fails)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	ASSERT_ARE_EQUAL(int, 0, umock_c_negative_tests_init());

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	OPTIONHANDLER_HANDLE value = TEST_OPTIONHANDLER_HANDLE;

	umock_c_reset_all_calls();
	set_expected_calls_for_device_set_option(handle, config, DEVICE_OPTION_SAVED_OPTIONS, &value);
	umock_c_negative_tests_snapshot();

	umock_c_negative_tests_reset();
	umock_c_negative_tests_fail_call(0);

	// act
	int result = device_set_option(handle, DEVICE_OPTION_SAVED_OPTIONS, &value);

	// assert
	//ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	umock_c_negative_tests_deinit();
	umock_c_reset_all_calls();

	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_076: [If `handle` is NULL, device_unsubscribe_message shall return a non-zero result]
TEST_FUNCTION(device_unsubscribe_message_NULL_handle)
{
	// arrange
	umock_c_reset_all_calls();

	// act
	int result = device_unsubscribe_message(NULL);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);

	// cleanup
}

// Tests_SRS_DEVICE_09_077: [messenger_unsubscribe_for_messages shall be invoked passing `instance->messenger_handle`]
// Tests_SRS_DEVICE_09_079: [If no failures occur, device_unsubscribe_message shall return 0]
TEST_FUNCTION(device_unsubscribe_message_succeess)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();

	STRICT_EXPECTED_CALL(messenger_unsubscribe_for_messages(TEST_MESSENGER_HANDLE))
		.SetReturn(0);

	// act
	int result = device_unsubscribe_message(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_078: [If messenger_unsubscribe_for_messages fails, device_unsubscribe_message shall return a non-zero result]
TEST_FUNCTION(device_unsubscribe_message_failure_checks)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();

	STRICT_EXPECTED_CALL(messenger_unsubscribe_for_messages(TEST_MESSENGER_HANDLE))
		.SetReturn(1);

	// act
	int result = device_unsubscribe_message(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_051: [If `handle` or `message` are NULL, device_send_event_async shall return a non-zero result]
TEST_FUNCTION(messenger_send_async_NULL_handle)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();

	// act
	int result = device_send_event_async(NULL, TEST_IOTHUB_MESSAGE_LIST, TEST_on_device_d2c_event_send_complete_callback, TEST_ON_DEVICE_EVENT_SEND_COMPLETE_CONTEXT);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_051: [If `handle` or `message` are NULL, device_send_event_async shall return a non-zero result]
TEST_FUNCTION(messenger_send_async_NULL_message)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();

	// act
	int result = device_send_event_async(handle, NULL, TEST_on_device_d2c_event_send_complete_callback, TEST_ON_DEVICE_EVENT_SEND_COMPLETE_CONTEXT);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_NOT_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_053: [If `send_task` fails to be created, device_send_event_async shall return a non-zero value]
// Tests_SRS_DEVICE_09_056: [If messenger_send_async fails, device_send_event_async shall return a non-zero value]
// Tests_SRS_DEVICE_09_057: [If any failures occur, device_send_event_async shall release all memory it has allocated]
TEST_FUNCTION(messenger_send_async_failure_checks)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	ASSERT_ARE_EQUAL(int, 0, umock_c_negative_tests_init());

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_send_async(config);
	umock_c_negative_tests_snapshot();

	// act
	size_t i;
	for (i = 0; i < umock_c_negative_tests_call_count(); i++)
	{
		// arrange 
		char error_msg[64];

		umock_c_negative_tests_reset();
		umock_c_negative_tests_fail_call(i);

		int result = device_send_event_async(handle, TEST_IOTHUB_MESSAGE_LIST, TEST_on_device_d2c_event_send_complete_callback, TEST_ON_DEVICE_EVENT_SEND_COMPLETE_CONTEXT);

		// assert
		sprintf(error_msg, "On failed call %zu", i);
		ASSERT_ARE_NOT_EQUAL_WITH_MSG(int, 0, result, error_msg);
	}

	// cleanup
	umock_c_negative_tests_deinit();
	umock_c_reset_all_calls();

	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_052: [A structure (`send_task`) shall be created to track the send state of the message]
// Tests_SRS_DEVICE_09_054: [`send_task` shall contain the user callback and the context provided]
// Tests_SRS_DEVICE_09_055: [The message shall be sent using messenger_send_async, passing `on_event_send_complete_messenger_callback` and `send_task`]
// Tests_SRS_DEVICE_09_058: [If no failures occur, device_send_event_async shall return 0]
TEST_FUNCTION(messenger_send_async_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_send_async(config);

	// act
	int result = device_send_event_async(handle, TEST_IOTHUB_MESSAGE_LIST, TEST_on_device_d2c_event_send_complete_callback, TEST_ON_DEVICE_EVENT_SEND_COMPLETE_CONTEXT);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, 0, result);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	EXPECTED_CALL(free(IGNORED_PTR_ARG));
	TEST_messenger_send_async_saved_callback(TEST_messenger_send_async_saved_message, MESSENGER_EVENT_SEND_COMPLETE_RESULT_OK, TEST_messenger_send_async_saved_context);

	device_destroy(handle);
}


// Tests_SRS_DEVICE_09_033: [If `handle` is NULL, device_do_work shall return]
TEST_FUNCTION(device_do_work_NULL_handle)
{
	// arrange
	umock_c_reset_all_calls();

	// act
	device_do_work(NULL);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
}

// Tests_SRS_DEVICE_09_034: [If CBS authentication is used and authentication state is AUTHENTICATION_STATE_STOPPED, authentication_start shall be invoked]
// Tests_SRS_DEVICE_09_035: [If authentication_start fails, the device state shall be updated to DEVICE_STATE_ERROR_AUTH]
TEST_FUNCTION(device_do_work_authentication_start_fails)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_do_work(config, TEST_current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STOPPED, MESSENGER_STATE_STOPPED);

	// act
	device_do_work(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_036: [If authentication state is AUTHENTICATION_STATE_STARTING, the device shall track the time since last event change and timeout if needed]
// Tests_SRS_DEVICE_09_037: [If authentication_start times out, the device state shall be updated to DEVICE_STATE_ERROR_AUTH_TIMEOUT]
TEST_FUNCTION(device_do_work_authentication_start_times_out)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	time_t next_time = add_seconds(TEST_current_time, DEFAULT_AUTH_STATE_CHANGED_TIMEOUT_SECS + 1);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_do_work(config, TEST_current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STOPPED, MESSENGER_STATE_STOPPED);

	device_do_work(handle);
	set_authentication_state(AUTHENTICATION_STATE_STOPPED, AUTHENTICATION_STATE_STARTING, TEST_current_time);

	set_expected_calls_for_device_do_work(config, next_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STARTING, MESSENGER_STATE_STOPPED);

	// act
	device_do_work(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTING, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_ERROR_AUTH_TIMEOUT, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_038: [If authentication state is AUTHENTICATION_STATE_ERROR and error code is AUTH_FAILED, the device state shall be updated to DEVICE_STATE_ERROR_AUTH]
TEST_FUNCTION(device_do_work_authentication_start_AUTH_FAILED)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_do_work(config, TEST_current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STOPPED, MESSENGER_STATE_STOPPED);
	device_do_work(handle);
	
	ASSERT_IS_NOT_NULL(TEST_authentication_create_saved_on_authentication_changed_callback);
	ASSERT_IS_NOT_NULL(TEST_authentication_create_saved_on_error_callback);

	set_authentication_state(AUTHENTICATION_STATE_STARTING, AUTHENTICATION_STATE_ERROR, TEST_current_time);
	TEST_authentication_create_saved_on_error_callback(TEST_authentication_create_saved_on_error_context, AUTHENTICATION_ERROR_AUTH_FAILED);

	// act
	device_do_work(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTING, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_ERROR_AUTH, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_039: [If authentication state is AUTHENTICATION_STATE_ERROR and error code is TIMEOUT, the device state shall be updated to DEVICE_STATE_ERROR_AUTH_TIMEOUT]
TEST_FUNCTION(device_do_work_authentication_start_AUTH_TIMEOUT)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	set_expected_calls_for_device_do_work(config, TEST_current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STOPPED, MESSENGER_STATE_STOPPED);
	device_do_work(handle);

	ASSERT_IS_NOT_NULL(TEST_authentication_create_saved_on_authentication_changed_callback);
	ASSERT_IS_NOT_NULL(TEST_authentication_create_saved_on_error_callback);

	set_authentication_state(AUTHENTICATION_STATE_STARTING, AUTHENTICATION_STATE_ERROR, TEST_current_time);
	TEST_authentication_create_saved_on_error_callback(TEST_authentication_create_saved_on_error_context, AUTHENTICATION_ERROR_AUTH_TIMEOUT);

	// act
	device_do_work(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTING, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_ERROR_AUTH_TIMEOUT, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_041: [If messenger state is MESSENGER_STATE_STOPPED, messenger_start shall be invoked]
// Tests_SRS_DEVICE_09_042: [If messenger_start fails, the device state shall be updated to DEVICE_STATE_ERROR_MSG]
// Tests_SRS_DEVICE_09_045: [If messenger state is MESSENGER_STATE_ERROR, the device state shall be updated to DEVICE_STATE_ERROR_MSG]
TEST_FUNCTION(device_do_work_messenger_start_FAILED)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	crank_device_do_work(handle, config, TEST_current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STOPPED, MESSENGER_STATE_STOPPED);
	set_authentication_state(AUTHENTICATION_STATE_STOPPED, AUTHENTICATION_STATE_STARTING, TEST_current_time);
	set_authentication_state(AUTHENTICATION_STATE_STARTING, AUTHENTICATION_STATE_STARTED, TEST_current_time);

	crank_device_do_work(handle, config, TEST_current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_STOPPED);

	ASSERT_IS_NOT_NULL(TEST_messenger_create_saved_on_state_changed_callback);

	set_messenger_state(MESSENGER_STATE_STOPPED, MESSENGER_STATE_STARTING, TEST_current_time);
	set_messenger_state(MESSENGER_STATE_STARTING, MESSENGER_STATE_ERROR, TEST_current_time);

	set_expected_calls_for_device_do_work(config, TEST_current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_ERROR);

	// act
	device_do_work(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTING, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_ERROR_MSG, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_043: [If messenger state is MESSENGER_STATE_STARTING, the device shall track the time since last event change and timeout if needed]
// Tests_SRS_DEVICE_09_044: [If messenger_start times out, the device state shall be updated to DEVICE_STATE_ERROR_MSG]
TEST_FUNCTION(device_do_work_messenger_start_timeout)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	time_t next_time = add_seconds(TEST_current_time, DEFAULT_MSGR_STATE_CHANGED_TIMEOUT_SECS + 1);

	umock_c_reset_all_calls();
	crank_device_do_work(handle, config, TEST_current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STOPPED, MESSENGER_STATE_STOPPED);
	set_authentication_state(AUTHENTICATION_STATE_STOPPED, AUTHENTICATION_STATE_STARTING, TEST_current_time);
	set_authentication_state(AUTHENTICATION_STATE_STARTING, AUTHENTICATION_STATE_STARTED, TEST_current_time);

	crank_device_do_work(handle, config, TEST_current_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_STOPPED);

	ASSERT_IS_NOT_NULL(TEST_messenger_create_saved_on_state_changed_callback);
	set_messenger_state(MESSENGER_STATE_STOPPED, MESSENGER_STATE_STARTING, TEST_current_time);

	set_expected_calls_for_device_do_work(config, next_time, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_STARTING);

	// act
	device_do_work(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTING, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_ERROR_MSG, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_040: [Messenger shall not be started if using CBS authentication and authentication start has not completed yet]
// Tests_SRS_DEVICE_09_046: [If messenger state is MESSENGER_STATE_STARTED, the device state shall be updated to DEVICE_STATE_STARTED]
// Tests_SRS_DEVICE_09_049: [If CBS is used for authentication and `instance->authentication_handle` state is not STOPPED or ERROR, authentication_do_work shall be invoked]
// Tests_SRS_DEVICE_09_050: [If `instance->messenger_handle` state is not STOPPED or ERROR, authentication_do_work shall be invoked]
TEST_FUNCTION(device_do_work_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_device(config, TEST_current_time);

	time_t t0 = TEST_current_time;
	time_t t1 = add_seconds(t0, DEFAULT_AUTH_STATE_CHANGED_TIMEOUT_SECS - 1);
	time_t t2 = add_seconds(t1, DEFAULT_MSGR_STATE_CHANGED_TIMEOUT_SECS - 2);
	time_t t3 = add_seconds(t2, DEFAULT_MSGR_STATE_CHANGED_TIMEOUT_SECS - 1);
	time_t t4 = add_seconds(t3, DEFAULT_MSGR_STATE_CHANGED_TIMEOUT_SECS + 1);

	umock_c_reset_all_calls();
	crank_device_do_work(handle, config, t0, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STOPPED, MESSENGER_STATE_STOPPED);
	set_authentication_state(AUTHENTICATION_STATE_STOPPED, AUTHENTICATION_STATE_STARTING, t0);

	crank_device_do_work(handle, config, t1, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STARTING, MESSENGER_STATE_STOPPED);
	set_authentication_state(AUTHENTICATION_STATE_STARTING, AUTHENTICATION_STATE_STARTED, t1);

	crank_device_do_work(handle, config, t2, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_STOPPED);
	set_messenger_state(MESSENGER_STATE_STOPPED, MESSENGER_STATE_STARTING, t2);

	crank_device_do_work(handle, config, t3, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_STARTING);
	set_messenger_state(MESSENGER_STATE_STARTING, MESSENGER_STATE_STARTED, t3);

	set_expected_calls_for_device_do_work(config, t4, DEVICE_STATE_STARTING, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_STARTED);

	// act
	device_do_work(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTING, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTED, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}


// Tests_SRS_DEVICE_09_047: [If CBS authentication is used and authentication state is not AUTHENTICATION_STATE_STARTED, the device state shall be updated to DEVICE_STATE_ERROR_AUTH]
TEST_FUNCTION(device_do_work_STARTED_auth_unexpected_state)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_and_crank_device(config, TEST_current_time);

	TEST_authentication_create_saved_on_error_callback(TEST_authentication_create_saved_on_error_context, AUTHENTICATION_ERROR_SAS_REFRESH_TIMEOUT);
	set_authentication_state(AUTHENTICATION_STATE_STARTED, AUTHENTICATION_STATE_ERROR, TEST_current_time);

	set_expected_calls_for_device_do_work(config, TEST_current_time, DEVICE_STATE_STARTED, AUTHENTICATION_STATE_ERROR, MESSENGER_STATE_STARTED);

	// act
	device_do_work(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTED, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_ERROR_AUTH, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_048: [If messenger state is not MESSENGER_STATE_STARTED, the device state shall be updated to DEVICE_STATE_ERROR_MSG]
TEST_FUNCTION(device_do_work_STARTED_messenger_unexpected_state)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_and_crank_device(config, TEST_current_time);

	set_messenger_state(MESSENGER_STATE_STARTED, MESSENGER_STATE_ERROR, TEST_current_time);


	set_expected_calls_for_device_do_work(config, TEST_current_time, DEVICE_STATE_STARTED, AUTHENTICATION_STATE_STARTED, MESSENGER_STATE_ERROR);

	// act
	device_do_work(handle);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_STARTED, TEST_on_state_changed_callback_saved_previous_state);
	ASSERT_ARE_EQUAL(int, DEVICE_STATE_ERROR_MSG, TEST_on_state_changed_callback_saved_new_state);
	ASSERT_IS_NOT_NULL(handle);

	// cleanup
	device_destroy(handle);
}


// Tests_SRS_DEVICE_09_059: [If `ev_send_comp_result` is MESSENGER_EVENT_SEND_COMPLETE_RESULT_OK, D2C_EVENT_SEND_COMPLETE_RESULT_OK shall be reported as `event_send_complete`]
// Tests_SRS_DEVICE_09_060: [If `ev_send_comp_result` is MESSENGER_EVENT_SEND_COMPLETE_RESULT_ERROR_CANNOT_PARSE, D2C_EVENT_SEND_COMPLETE_RESULT_ERROR_CANNOT_PARSE shall be reported as `event_send_complete`]
// Tests_SRS_DEVICE_09_061: [If `ev_send_comp_result` is MESSENGER_EVENT_SEND_COMPLETE_RESULT_ERROR_FAIL_SENDING, D2C_EVENT_SEND_COMPLETE_RESULT_ERROR_FAIL_SENDING shall be reported as `event_send_complete`]
// Tests_SRS_DEVICE_09_062: [If `ev_send_comp_result` is MESSENGER_EVENT_SEND_COMPLETE_RESULT_ERROR_TIMEOUT, D2C_EVENT_SEND_COMPLETE_RESULT_ERROR_TIMEOUT shall be reported as `event_send_complete`]
// Tests_SRS_DEVICE_09_063: [If `ev_send_comp_result` is MESSENGER_EVENT_SEND_COMPLETE_RESULT_MESSENGER_DESTROYED, D2C_EVENT_SEND_COMPLETE_RESULT_DEVICE_DESTROYED shall be reported as `event_send_complete`]
// Tests_SRS_DEVICE_09_064: [If provided, the user callback and context saved in `send_task` shall be invoked passing the device `event_send_complete`]
// Tests_SRS_DEVICE_09_065: [The memory allocated for `send_task` shall be released]
TEST_FUNCTION(on_event_send_complete_messenger_callback_succeeds)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_device(config, TEST_current_time);

	MESSENGER_EVENT_SEND_COMPLETE_RESULT messenger_results[5];
	messenger_results[0] = MESSENGER_EVENT_SEND_COMPLETE_RESULT_OK;
	messenger_results[1] = MESSENGER_EVENT_SEND_COMPLETE_RESULT_ERROR_CANNOT_PARSE;
	messenger_results[2] = MESSENGER_EVENT_SEND_COMPLETE_RESULT_ERROR_FAIL_SENDING;
	messenger_results[3] = MESSENGER_EVENT_SEND_COMPLETE_RESULT_ERROR_TIMEOUT;
	messenger_results[4] = MESSENGER_EVENT_SEND_COMPLETE_RESULT_MESSENGER_DESTROYED;

	D2C_EVENT_SEND_RESULT device_results[5];
	device_results[0] = D2C_EVENT_SEND_COMPLETE_RESULT_OK;
	device_results[1] = D2C_EVENT_SEND_COMPLETE_RESULT_ERROR_CANNOT_PARSE;
	device_results[2] = D2C_EVENT_SEND_COMPLETE_RESULT_ERROR_FAIL_SENDING;
	device_results[3] = D2C_EVENT_SEND_COMPLETE_RESULT_ERROR_TIMEOUT;
	device_results[4] = D2C_EVENT_SEND_COMPLETE_RESULT_DEVICE_DESTROYED;

	// act
	int i;
	for (i = 0; i < 5; i++)
	{
		printf("Verifying on_event_send_complete for enum value %d\r\n", i);

		umock_c_reset_all_calls();
		set_expected_calls_for_device_send_async(config);

		int result = device_send_event_async(handle, TEST_IOTHUB_MESSAGE_LIST, TEST_on_device_d2c_event_send_complete_callback, TEST_ON_DEVICE_EVENT_SEND_COMPLETE_CONTEXT);
		ASSERT_ARE_EQUAL(int, 0, result);

		ASSERT_IS_NOT_NULL(TEST_messenger_send_async_saved_callback);
		
		EXPECTED_CALL(free(IGNORED_PTR_ARG));
		TEST_messenger_send_async_saved_callback(TEST_messenger_send_async_saved_message, messenger_results[i], TEST_messenger_send_async_saved_context);

		ASSERT_ARE_EQUAL(int, device_results[i], TEST_on_device_d2c_event_send_complete_callback_saved_result);
		ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	}

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_070: [If `iothub_message_handle` or `context` is NULL, on_messenger_message_received_callback shall return MESSENGER_DISPOSITION_RESULT_ABANDONED]
TEST_FUNCTION(on_messenger_message_received_callback_NULL_handle)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_and_crank_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	TEST_messenger_subscribe_for_messages_return = 0;
	STRICT_EXPECTED_CALL(messenger_subscribe_for_messages(TEST_MESSENGER_HANDLE, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.IgnoreArgument(2)
		.IgnoreArgument(3);

	(void)device_subscribe_message(handle, TEST_on_message_received, handle);
	ASSERT_IS_NOT_NULL(TEST_messenger_subscribe_for_messages_saved_on_message_received_callback);

	umock_c_reset_all_calls();

	// act
	MESSENGER_DISPOSITION_RESULT result = TEST_messenger_subscribe_for_messages_saved_on_message_received_callback(
		NULL,
		TEST_messenger_subscribe_for_messages_saved_context);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, MESSENGER_DISPOSITION_RESULT_ABANDONED, result);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_070: [If `iothub_message_handle` or `context` is NULL, on_messenger_message_received_callback shall return MESSENGER_DISPOSITION_RESULT_ABANDONED]
TEST_FUNCTION(on_messenger_message_received_callback_NULL_context)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_and_crank_device(config, TEST_current_time);

	umock_c_reset_all_calls();
	TEST_messenger_subscribe_for_messages_return = 0;
	STRICT_EXPECTED_CALL(messenger_subscribe_for_messages(TEST_MESSENGER_HANDLE, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.IgnoreArgument(2)
		.IgnoreArgument(3);

	(void)device_subscribe_message(handle, TEST_on_message_received, handle);
	ASSERT_IS_NOT_NULL(TEST_messenger_subscribe_for_messages_saved_on_message_received_callback);

	umock_c_reset_all_calls();

	// act
	MESSENGER_DISPOSITION_RESULT result = TEST_messenger_subscribe_for_messages_saved_on_message_received_callback(
		TEST_IOTHUB_MESSAGE_HANDLE,
		NULL);

	// assert
	ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
	ASSERT_ARE_EQUAL(int, MESSENGER_DISPOSITION_RESULT_ABANDONED, result);

	// cleanup
	device_destroy(handle);
}

// Tests_SRS_DEVICE_09_071: [The user callback shall be invoked, passing the context it provided]
// Tests_SRS_DEVICE_09_072: [If the user callback returns DEVICE_MESSAGE_DISPOSITION_RESULT_ACCEPTED, on_messenger_message_received_callback shall return MESSENGER_DISPOSITION_RESULT_ACCEPTED]
// Tests_SRS_DEVICE_09_073: [If the user callback returns DEVICE_MESSAGE_DISPOSITION_RESULT_REJECTED, on_messenger_message_received_callback shall return MESSENGER_DISPOSITION_RESULT_REJECTED]
// Tests_SRS_DEVICE_09_074: [If the user callback returns DEVICE_MESSAGE_DISPOSITION_RESULT_ABANDONED, on_messenger_message_received_callback shall return MESSENGER_DISPOSITION_RESULT_ABANDONED]
TEST_FUNCTION(on_messenger_message_received_callback_succeess)
{
	// arrange
	ASSERT_IS_TRUE_WITH_MSG(INDEFINITE_TIME != TEST_current_time, "Failed setting TEST_current_time");

	DEVICE_CONFIG* config = get_device_config(DEVICE_AUTH_MODE_CBS, true);
	DEVICE_HANDLE handle = create_and_start_and_crank_device(config, TEST_current_time);

	MESSENGER_DISPOSITION_RESULT messenger_results[3];
	messenger_results[0] = MESSENGER_DISPOSITION_RESULT_ACCEPTED;
	messenger_results[1] = MESSENGER_DISPOSITION_RESULT_REJECTED;
	messenger_results[2] = MESSENGER_DISPOSITION_RESULT_ABANDONED;

	DEVICE_MESSAGE_DISPOSITION_RESULT device_results[3];
	device_results[0] = DEVICE_MESSAGE_DISPOSITION_RESULT_ACCEPTED;
	device_results[1] = DEVICE_MESSAGE_DISPOSITION_RESULT_REJECTED;
	device_results[2] = DEVICE_MESSAGE_DISPOSITION_RESULT_ABANDONED;

	umock_c_reset_all_calls();
	TEST_messenger_subscribe_for_messages_return = 0;
	STRICT_EXPECTED_CALL(messenger_subscribe_for_messages(TEST_MESSENGER_HANDLE, IGNORED_PTR_ARG, IGNORED_PTR_ARG))
		.IgnoreArgument(2)
		.IgnoreArgument(3);
	
	(void)device_subscribe_message(handle, TEST_on_message_received, handle);
	ASSERT_IS_NOT_NULL(TEST_messenger_subscribe_for_messages_saved_on_message_received_callback);

	// act
	int i;
	for (i = 0; i < 3; i++)
	{
		printf("Verifying on_messenger_message_received_callback for enum value %d\r\n", i);

		TEST_on_message_received_return = device_results[i];

		umock_c_reset_all_calls();

		MESSENGER_DISPOSITION_RESULT result = TEST_messenger_subscribe_for_messages_saved_on_message_received_callback(
			TEST_IOTHUB_MESSAGE_HANDLE,
			TEST_messenger_subscribe_for_messages_saved_context);

		// assert
		ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
		ASSERT_ARE_EQUAL(int, messenger_results[i], result);
	}

	// cleanup
	device_destroy(handle);
}

END_TEST_SUITE(iothubtransport_amqp_device_ut)
