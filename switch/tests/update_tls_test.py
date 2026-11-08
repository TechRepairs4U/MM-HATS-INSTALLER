"""Exercise the updater's actual TLS option block with host libcurl and GitHub."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
source = (repo / 'sphaira/source/download.cpp').read_text()
start = source.index('if (e.GetFlags() & Flag_VerifyTls) {')
start = source.index('\n', start) + 1
block = source[start:source.index('\n    }', start)]
block = block.replace('"sdmc:/config/mm-tools/hats-ca-bundle.pem"', 'argv[1]')
test = r'''
#include <curl/curl.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#define CURL_EASY_SETOPT_LOG(c, key, value) assert(curl_easy_setopt(c, key, value) == CURLE_OK)
int main(int argc, char** argv) {
    assert(argc == 3);
    auto* curl = curl_easy_init();
    assert(curl);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/TechRepairs4U/MM-HATS-INSTALLER/releases/latest");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MM-HATS-Update-Test");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char*, size_t size, size_t n, void*) { return size * n; });
''' + block + r'''
    const auto rc = curl_easy_perform(curl);
    if (std::atoi(argv[2])) {
        long http{}, verification{};
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
        curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &verification);
        assert(rc == CURLE_OK && http == 200 && verification == 0);
    } else assert(rc != CURLE_OK);
    curl_easy_cleanup(curl);
}
'''
with tempfile.TemporaryDirectory(prefix='mm-update-tls-') as directory:
    folder = Path(directory)
    (folder / 'test.cpp').write_text(test)
    (folder / 'invalid.pem').write_text('not a certificate\n')
    binary = folder / 'test'
    subprocess.run(['g++', '-std=c++20', '-Wno-deprecated-declarations', str(folder / 'test.cpp'), '-lcurl', '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(repo / 'assets/romfs/hats-ca-bundle.pem'), '1'], check=True)
    subprocess.run([str(binary), str(folder / 'invalid.pem'), '0'], check=True)
print('Updater TLS options: verified GitHub request passed; invalid CA rejected.')
