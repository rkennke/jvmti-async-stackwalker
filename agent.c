#define _GNU_SOURCE
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "jvmti.h"
#include "jni.h"

#define SIG SIGRTMIN

//Callback method
void VMInit(jvmtiEnv* jvmti_env, JNIEnv* jni_env, jthread thread)
{
  // Nothing to do here.
}

jvmtiEnv* _env = NULL;
jlong _id = 0;
jvmtiExtensionFunction _request_stack_trace = NULL;
jvmtiExtensionFunction _init_request_stack_trace = NULL;

static void handler(int signo, siginfo_t* info, void* context) {
  if (_request_stack_trace != NULL) {
    jvmtiError err = _request_stack_trace(_env, NULL, context, _id++);
    if (err != JVMTI_ERROR_NONE) {
      printf("Error in RequestStackTrace: %d\n", err);
    }
  }
}

void MethodEntry(jvmtiEnv* jvmti_env, JNIEnv* jni_env, jthread thread, jmethodID method) {
  jvmtiError error;
  char* name_p;
  char* signature_p;
  char* generic_p;

  error = (*jvmti_env)->GetMethodName(jvmti_env, method, &name_p, &signature_p, &generic_p);

  if (strcmp(name_p, "main") == 0) {
    // Establish signal handler.
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIG, &sa, NULL) == -1) {
      printf("error in sigaction\n");
      return;
    }

    // Block timer temporarily.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIG);
    if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1) {
      printf("error in sigprocmask\n");
      return;
    }

    // Create the timer.
    timer_t timerid;
    struct sigevent sev;
    pid_t thread_id = gettid();
    sev.sigev_notify = SIGEV_THREAD_ID;
    sev.sigev_signo = SIG;
    sev.sigev_value.sival_ptr = jvmti_env;
    ((int*) &sev.sigev_notify)[1] /* sev.sigev_notify_thread_id */ = thread_id;
    if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0) {
      printf("error in timer_create\n");
      return;
    }

    struct itimerspec its;
    its.it_interval.tv_sec = 1;
    its.it_interval.tv_nsec = 0;
    its.it_value.tv_sec = 1;
    its.it_value.tv_nsec = 1;
    if (timer_settime(timerid, 0, &its, NULL) == -1) {
      printf("error in timer_settime\n");
    }

    sleep(1);

    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
      printf("error in sigprocmask");
      return;
    }

    if ((*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_DISABLE,
					       JVMTI_EVENT_METHOD_ENTRY, (jthread)NULL)) {
      printf("error in SetEventNotificationMode");
      return;
    }
  }
}

jint Agent_OnLoad(JavaVM* vm, char* options, void* reserved) {
  jvmtiEnv* environment;
  jvmtiEventCallbacks callbacks;
  jvmtiError error;
  jint result;

  // Get JVMTI environment
  result = (*vm)->GetEnv(vm, (void**) &environment, JVMTI_VERSION_1_2);
  if (result != 0) {
    printf("error in GetEnv\n");
    return JNI_ERR;
  }

  _env = environment;

  // Find extension functions.
  jint extension_count;
  jvmtiExtensionFunctionInfo* extensions;
  error = (*_env)->GetExtensionFunctions(_env, &extension_count, &extensions);
  if (error != JVMTI_ERROR_NONE) {
    printf("Error in GetExtensionFunctions: %d\n", error);
    return JNI_ERR;
  }

  for (jint i = 0; i < extension_count; i++) {
    jvmtiExtensionFunctionInfo* ext_info = &extensions[i];
    if (strcmp(ext_info->id, "com.sun.hotspot.functions.RequestStackTrace") == 0) {
      _request_stack_trace = ext_info->func;
    }
    if (strcmp(ext_info->id, "com.sun.hotspot.functions.InitializeRequestStackTrace") == 0) {
      _init_request_stack_trace = ext_info->func;
    }
    printf("Extension %d: id: %s, short description: %s\n", i, ext_info->id, ext_info->short_description);
    for (jint param_id = 0; param_id < ext_info->param_count; param_id++) {
      jvmtiParamInfo* param = &(ext_info->params[param_id]);
      printf("  param: %d: name: %s\n", param_id, param->name);
      (*_env)->Deallocate(_env, param->name);
    }
    (*_env)->Deallocate(_env, ext_info->id);
    (*_env)->Deallocate(_env, ext_info->short_description);
    (*_env)->Deallocate(_env, (unsigned char*)ext_info->params);
    (*_env)->Deallocate(_env, (unsigned char*)ext_info->errors);
  }
  (*_env)->Deallocate(_env, (unsigned char*)extensions);

  // Setup JVMTI capabilities
  jvmtiCapabilities capabilities;
  memset(&capabilities, 0, sizeof(capabilities));
  capabilities.can_generate_method_entry_events       = 1;
  error = (*environment)->AddCapabilities(environment, &capabilities);
  if (error != JNI_OK) {
    printf("error in AddCapabilities\n");
  }

  // Initialize requesting of stack-traces.
  if (_init_request_stack_trace != NULL) {
    _init_request_stack_trace(_env);
  } else {
    printf("Count not find InitializeRequestStackTrace extension function\n");
    return JNI_ERR;
  }

  // Register callbacks
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.VMInit = (void*) &VMInit;
  callbacks.MethodEntry = (void*) &MethodEntry;

  error = (*environment)->SetEventCallbacks(environment, &callbacks, (jint) sizeof(callbacks));
  if (error != JNI_OK) {
    printf("error in SetEventCallbacks\n");
    return JNI_ERR;
  }

  // Set notifications
  error = (*environment)->SetEventNotificationMode(environment, JVMTI_ENABLE,
						   JVMTI_EVENT_VM_INIT, (jthread) NULL);
  if (error != JNI_OK) {
    printf("error in SetEventNotificationMode\n");
    return JNI_ERR;
  }
  error = (*environment)->SetEventNotificationMode(environment, JVMTI_ENABLE,
						   JVMTI_EVENT_METHOD_ENTRY, (jthread) NULL);
  if (error != JNI_OK) {
    printf("error in SetEventNotificationMode\n");
    return JNI_ERR;
  }
  return JNI_OK;
}

// Call when agent is unloaded
void Agent_OnUnload(JavaVM* vm) {
  printf("\nAgent Unloaded\n");
}
