package com.example.bedrockai.companion

/** Runs the native v0.35 ControlHub without trusting or inventing game observations. */
object CompanionBrainBridge {
    fun selfCheck(): String {
        val report = BedrockCompanionNative.nativeSelfCheck()
        check(report.isNotBlank()) { "Native ControlHub returned an empty self-check report" }
        return report
    }
}
