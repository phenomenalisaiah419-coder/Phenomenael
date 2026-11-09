package com.example.bedrockai

import javax.crypto.KeyAgreement
import java.security.KeyFactory
import java.security.KeyPairGenerator
import java.security.PrivateKey
import java.security.PublicKey
import java.security.SecureRandom
import java.security.Signature
import java.security.spec.ECGenParameterSpec
import java.security.spec.EncodedKeySpec
import java.security.spec.PKCS8EncodedKeySpec
import java.security.spec.X509EncodedKeySpec
import java.security.MessageDigest

/** Android platform crypto provider used by the native Bedrock protocol layer. */
class AndroidJcaCrypto {
    fun generateP384KeyPair(): Array<ByteArray> {
        val generator = KeyPairGenerator.getInstance("EC")
        generator.initialize(ECGenParameterSpec("secp384r1"), SecureRandom())
        val pair = generator.generateKeyPair()
        return arrayOf(pair.private.encoded, pair.public.encoded)
    }

    fun signEs384(privatePkcs8: ByteArray, data: ByteArray): ByteArray {
        val key = decodePrivate(privatePkcs8)
        val signer = Signature.getInstance("SHA384withECDSA")
        signer.initSign(key, SecureRandom())
        signer.update(data)
        return derToRawRs(signer.sign(), 48)
    }

    fun verifyEs384(publicPkix: ByteArray, data: ByteArray, rawRs: ByteArray): Boolean {
        if (rawRs.size != 96) return false
        val key = decodePublic(publicPkix)
        val verifier = Signature.getInstance("SHA384withECDSA")
        verifier.initVerify(key)
        verifier.update(data)
        return verifier.verify(rawRsToDer(rawRs, 48))
    }

    fun deriveP384SharedSecretX(privatePkcs8: ByteArray, peerPkix: ByteArray): ByteArray {
        val privateKey = decodePrivate(privatePkcs8)
        val publicKey = decodePublic(peerPkix)
        val agreement = KeyAgreement.getInstance("ECDH")
        agreement.init(privateKey)
        agreement.doPhase(publicKey, true)
        val fullSecret = agreement.generateSecret()
        require(fullSecret.size >= 48) { "unexpected P-384 shared-secret length" }
        return fullSecret.copyOfRange(fullSecret.size - 48, fullSecret.size)
    }

    fun sha256(data: ByteArray): ByteArray = MessageDigest.getInstance("SHA-256").digest(data)

    private fun decodePrivate(encoded: ByteArray): PrivateKey {
        val factory = KeyFactory.getInstance("EC")
        val spec: EncodedKeySpec = PKCS8EncodedKeySpec(encoded)
        return factory.generatePrivate(spec)
    }

    private fun decodePublic(encoded: ByteArray): PublicKey {
        val factory = KeyFactory.getInstance("EC")
        val spec: EncodedKeySpec = X509EncodedKeySpec(encoded)
        return factory.generatePublic(spec)
    }

    private fun derToRawRs(der: ByteArray, componentSize: Int): ByteArray {
        require(componentSize > 0) { "component width must be positive" }
        require(der.size >= 2) { "ECDSA signature is truncated" }
        var p = 0
        require(der[p++].toInt() and 0xff == 0x30) { "ECDSA signature is not a SEQUENCE" }
        val (sequenceLength, next) = readDerLength(der, p)
        p = next
        require(sequenceLength == der.size - p) { "invalid ECDSA sequence length" }
        require(der[p++].toInt() and 0xff == 0x02) { "missing ECDSA r" }
        val (rLength, rNext) = readDerLength(der, p)
        p = rNext
        require(p + rLength <= der.size) { "invalid ECDSA r length" }
        val r = der.copyOfRange(p, p + rLength)
        p += rLength
        require(der[p++].toInt() and 0xff == 0x02) { "missing ECDSA s" }
        val (sLength, sNext) = readDerLength(der, p)
        p = sNext
        require(p + sLength == der.size) { "invalid ECDSA s length" }
        require(r.isNotEmpty() && sLength > 0) { "ECDSA components cannot be empty" }
        val s = der.copyOfRange(p, p + sLength)
        return unsignedFixedWidth(r, componentSize) + unsignedFixedWidth(s, componentSize)
    }

    private fun rawRsToDer(raw: ByteArray, componentSize: Int): ByteArray {
        require(componentSize > 0 && raw.size == componentSize * 2) { "invalid raw ECDSA signature width" }
        val r = raw.copyOfRange(0, componentSize)
        val s = raw.copyOfRange(componentSize, componentSize * 2)
        val rDer = derInteger(r)
        val sDer = derInteger(s)
        val body = rDer + sDer
        return byteArrayOf(0x30, *derLength(body.size)) + body
    }

    private fun derInteger(raw: ByteArray): ByteArray {
        require(raw.isNotEmpty()) { "DER integer cannot be empty" }
        var start = 0
        while (start < raw.lastIndex && raw[start].toInt() == 0) start++
        var value = raw.copyOfRange(start, raw.size)
        if ((value[0].toInt() and 0x80) != 0) value = byteArrayOf(0) + value
        return byteArrayOf(0x02, *derLength(value.size)) + value
    }

    private fun unsignedFixedWidth(encoded: ByteArray, width: Int): ByteArray {
        require(width > 0) { "ECDSA component width must be positive" }
        require(encoded.isNotEmpty()) { "ECDSA component cannot be empty" }
        var start = 0
        while (start < encoded.lastIndex && encoded[start].toInt() == 0) start++
        val value = encoded.copyOfRange(start, encoded.size)
        require(value.size <= width) { "ECDSA component exceeds P-384 width" }
        return ByteArray(width - value.size) + value
    }

    private fun readDerLength(bytes: ByteArray, offset: Int): Pair<Int, Int> {
        require(offset < bytes.size) { "missing DER length" }
        val first = bytes[offset].toInt() and 0xff
        if (first < 0x80) return Pair(first, offset + 1)
        val count = first and 0x7f
        require(count in 1..4 && offset + count < bytes.size) { "unsupported DER length" }
        var value = 0
        for (i in 0 until count) value = (value shl 8) or (bytes[offset + 1 + i].toInt() and 0xff)
        require(value >= 0) { "invalid DER length" }
        return Pair(value, offset + 1 + count)
    }

    private fun derLength(length: Int): ByteArray {
        require(length >= 0)
        if (length < 0x80) return byteArrayOf(length.toByte())
        var value = length
        val tmp = ByteArray(4)
        var count = 0
        while (value != 0) {
            tmp[3 - count] = (value and 0xff).toByte()
            value = value ushr 8
            count++
        }
        require(count <= 4)
        return byteArrayOf((0x80 or count).toByte()) + tmp.copyOfRange(4 - count, 4)
    }
}
