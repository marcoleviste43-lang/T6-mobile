plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.tekken6.online"
    compileSdk = 34
    defaultConfig {
        applicationId = "com.tekken6.online"
        minSdk = 26
        targetSdk = 34
        // CI run number keeps versionCode rising on every build, so updates install over older builds.
        versionCode = (System.getenv("GITHUB_RUN_NUMBER")?.toIntOrNull() ?: 1) + 100
        versionName = "0.2.0-prototype"
    }
    // One fixed debug key, committed on purpose (public "android" passwords, prototype only). Without it every
    // CI build is signed with a fresh random key and Android reports a package conflict when you update.
    signingConfigs {
        getByName("debug") {
            storeFile = file("debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
