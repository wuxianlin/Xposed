/**
 * This file includes functions specific to the ART runtime.
 */

#include "xposed_shared.h"
#include "libxposed_common.h"

#include "thread.h"
#include "common_throws.h"
#if PLATFORM_SDK_VERSION >= 23
#include "art_method-inl.h"
#else
#include "mirror/art_method-inl.h"
#endif
#include "mirror/object-inl.h"
#include "mirror/throwable.h"
#include "native/scoped_fast_native_object_access.h"
#include "reflection.h"
#include "scoped_thread_state_change.h"
#include "well_known_classes.h"

using namespace art;

#if PLATFORM_SDK_VERSION < 23
using art::mirror::ArtMethod;
#endif

namespace xposed {


////////////////////////////////////////////////////////////
// Library initialization
////////////////////////////////////////////////////////////

/** Called by Xposed's app_process replacement. */
bool xposedInitLib(XposedShared* shared) {
    xposed = shared;
    xposed->onVmCreated = &onVmCreatedCommon;
    return true;
}

/** Called very early during VM startup. */
bool onVmCreated(JNIEnv* env) {

    jclass classMiuiResources = env->FindClass(CLASS_MIUI_RESOURCES);
    if (classMiuiResources != NULL) {
        ScopedObjectAccess soa(env);
        mirror::Class* clazz = soa.Decode<mirror::Class*>(classMiuiResources);
        if (clazz->IsFinal()) {
            ALOGD("Removing final flag for class '%s'", CLASS_MIUI_RESOURCES);
            clazz->SetAccessFlags(clazz->GetAccessFlags() & ~kAccFinal);
        }
    }
    env->ExceptionClear();

    ArtMethod::xposed_callback_class = classXposedBridge;
    ArtMethod::xposed_callback_method = methodXposedBridgeHandleHookedMethod;
    return true;
}


////////////////////////////////////////////////////////////
// Utility methods
////////////////////////////////////////////////////////////
void logExceptionStackTrace() {
    Thread* self = Thread::Current();
#if PLATFORM_SDK_VERSION >= 23
    XLOG(ERROR) << self->GetException()->Dump();
#else
    XLOG(ERROR) << self->GetException(nullptr)->Dump();
#endif
}

/** Lay the foundations for XposedBridge.setObjectClassNative() */
void prepareSubclassReplacement(JNIEnv* env, jclass clazz) {
    // clazz is supposed to replace its superclass, so make sure enough memory is allocated
    ScopedObjectAccess soa(env);
    mirror::Class* sub = soa.Decode<mirror::Class*>(clazz);
    mirror::Class* super = sub->GetSuperClass();
    super->SetObjectSize(sub->GetObjectSize());
}


////////////////////////////////////////////////////////////
// JNI methods
////////////////////////////////////////////////////////////

void XposedBridge_hookMethodNative(JNIEnv* env, jclass, jobject javaReflectedMethod,
            jobject, jint, jobject javaAdditionalInfo) {
    // Detect usage errors.
    if (javaReflectedMethod == nullptr) {
#if PLATFORM_SDK_VERSION >= 23
        ThrowIllegalArgumentException("method must not be null");
#else
        ThrowIllegalArgumentException(nullptr, "method must not be null");
#endif
        return;
    }

    // Get the ArtMethod of the method to be hooked.
    ScopedObjectAccess soa(env);
    ArtMethod* artMethod = ArtMethod::FromReflectedMethod(soa, javaReflectedMethod);

    // Hook the method
    artMethod->EnableXposedHook(soa, javaAdditionalInfo);
}

jobject XposedBridge_invokeOriginalMethodNative(JNIEnv* env, jclass, jobject javaMethod,
            jint isResolved, jobjectArray, jclass, jobject javaReceiver, jobjectArray javaArgs) {
    ScopedFastNativeObjectAccess soa(env);
    if (UNLIKELY(!isResolved)) {
        ArtMethod* artMethod = ArtMethod::FromReflectedMethod(soa, javaMethod);
        if (LIKELY(artMethod->IsXposedHookedMethod())) {
            javaMethod = artMethod->GetXposedHookInfo()->reflectedMethod;
        }
    }
#if PLATFORM_SDK_VERSION >= 23
    return InvokeMethod(soa, javaMethod, javaReceiver, javaArgs);
#else
    return InvokeMethod(soa, javaMethod, javaReceiver, javaArgs, true);
#endif
}

void XposedBridge_setObjectClassNative(JNIEnv* env, jclass, jobject javaObj, jclass javaClazz) {
    ScopedObjectAccess soa(env);
    mirror::Class* clazz = soa.Decode<mirror::Class*>(javaClazz);
    StackHandleScope<1> hs(soa.Self());
    Handle<mirror::Class> c(hs.NewHandle(clazz));
#if PLATFORM_SDK_VERSION >= 23
    if (!Runtime::Current()->GetClassLinker()->EnsureInitialized(soa.Self(), c, true, true)) {
#else
    if (!Runtime::Current()->GetClassLinker()->EnsureInitialized(c, true, true)) {
#endif
        XLOG(ERROR) << "Could not initialize class " << PrettyClass(clazz);
        return;
    }
    mirror::Object* obj = soa.Decode<mirror::Object*>(javaObj);
    obj->SetClass(clazz);
}

void XposedBridge_dumpObjectNative(JNIEnv*, jclass, jobject) {
    // TODO Can be useful for debugging
    UNIMPLEMENTED(ERROR|LOG_XPOSED);
}

jobject XposedBridge_cloneToSubclassNative(JNIEnv* env, jclass, jobject javaObject, jclass javaClazz) {
    ScopedObjectAccess soa(env);
    mirror::Object* obj = soa.Decode<mirror::Object*>(javaObject);
    mirror::Class* clazz = soa.Decode<mirror::Class*>(javaClazz);
    mirror::Object* dest = obj->Clone(soa.Self(), clazz->GetObjectSize());
    dest->SetClass(clazz);
    return soa.AddLocalReference<jobject>(dest);
}

jint XposedBridge_getRuntime(JNIEnv*, jclass) {
    return 2; // RUNTIME_ART
}

}  // namespace xposed
