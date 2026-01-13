#include <jni.h>
#include <string.h>

static jint* pinned;

JNIEXPORT void JNICALL
Java_TestPinnedDeadlock_pin(JNIEnv *env, jclass unused, jintArray a) {
  pinned = (*env)->GetPrimitiveArrayCritical(env, a, 0);
}

JNIEXPORT void JNICALL
Java_TestPinnedDeadlock_unpin(JNIEnv *env, jclass unused, jintArray a) {
  (*env)->ReleasePrimitiveArrayCritical(env, a, pinned, 0);
}
