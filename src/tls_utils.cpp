#include "tls_utils.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_bin_start");

void TlsConfig::configureClient(WiFiClientSecure& client, uint32_t timeoutMs) {
    client.setCACertBundle(rootca_crt_bundle_start);
    client.setTimeout(timeoutMs);
}

const uint8_t* TlsConfig::caBundle() {
    return rootca_crt_bundle_start;
}
