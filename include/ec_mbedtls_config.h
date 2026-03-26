/*
 * mbedTLS configuration notes for EmbedClaw.
 *
 * Currently we use mbedTLS's default config (full feature set).
 * For production embedded builds, replace this with a minimal
 * client-only config to reduce binary size.  The modules needed are:
 *
 *   Protocol:   MBEDTLS_SSL_CLI_C, MBEDTLS_SSL_TLS_C, MBEDTLS_SSL_PROTO_TLS1_2
 *   X.509:      MBEDTLS_X509_CRT_PARSE_C, MBEDTLS_PEM_PARSE_C
 *   Key exch:   MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED,
 *               MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
 *   Crypto:     MBEDTLS_AES_C, MBEDTLS_GCM_C, MBEDTLS_RSA_C,
 *               MBEDTLS_ECDH_C, MBEDTLS_ECDSA_C, MBEDTLS_ECP_C
 *   Hashes:     MBEDTLS_SHA256_C, MBEDTLS_SHA384_C
 *   RNG:        MBEDTLS_CTR_DRBG_C, MBEDTLS_ENTROPY_C
 *   SNI:        MBEDTLS_SSL_SERVER_NAME_INDICATION
 *
 * To use a custom config, set MBEDTLS_CONFIG_FILE in CMakeLists.txt.
 */
