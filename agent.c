#define _GNU_SOURCE
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
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

static long perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                            int cpu, int group_fd, unsigned long flags) {
  return syscall(SYS_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

void MethodEntry(jvmtiEnv* jvmti_env, JNIEnv* jni_env, jthread thread, jmethodID method) {
}
void ThreadStart(jvmtiEnv* jvmti_env, JNIEnv* jni_env, jthread thread) {
  jvmtiError error;

    // Establish signal handler.
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handler;
    sigfillset(&sa.sa_mask);
    if (sigaction(SIG, &sa, NULL) == -1) {
      printf("error in sigaction\n");
      return;
    }

    // Setup perf event for cache misses
    struct perf_event_attr pe;
    memset(&pe, 0, sizeof(struct perf_event_attr));
    pe.type = PERF_TYPE_HARDWARE;
    pe.size = sizeof(struct perf_event_attr);
    pe.config = PERF_COUNT_HW_CACHE_MISSES;
    pe.sample_period = 100000; // Sample every 100K cache-misses
    pe.sample_type = PERF_SAMPLE_IP;
    pe.disabled = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    pid_t thread_id = gettid();
    int perf_fd = perf_event_open(&pe, thread_id, -1, -1, 0);
    if (perf_fd == -1) {
      printf("error in perf_event_open: %s\n", strerror(errno));
      return;
    }

    // Configure perf event to send signals
    struct f_owner_ex fown_ex;
    fown_ex.type = F_OWNER_TID;
    fown_ex.pid = thread_id;
    if (fcntl(perf_fd, F_SETOWN_EX, &fown_ex) == -1) {
      printf("error in F_SETOWN_EX: %s\n", strerror(errno));
      close(perf_fd);
      return;
    }

    if (fcntl(perf_fd, F_SETFL, O_ASYNC) == -1) {
      printf("error in F_SETFL: %s\n", strerror(errno));
      close(perf_fd);
      return;
    }

    if (fcntl(perf_fd, F_SETSIG, SIG) == -1) {
      printf("error in F_SETSIG: %s\n", strerror(errno));
      close(perf_fd);
      return;
    }

    // Enable the perf event
    ioctl(perf_fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0);

    printf("Cache-miss profiling enabled (sampling every 100K misses)\n");

    if ((*jvmti_env)->SetEventNotificationMode(jvmti_env, JVMTI_DISABLE,
					       JVMTI_EVENT_METHOD_ENTRY, (jthread)NULL)) {
      printf("error in SetEventNotificationMode");
      return;
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
  callbacks.ThreadStart = (void*) &ThreadStart;

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
  error = (*environment)->SetEventNotificationMode(environment, JVMTI_ENABLE,
						   JVMTI_EVENT_THREAD_START, (jthread) NULL);
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
