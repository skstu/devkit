# libfix notices

The integration layer was extracted and generalized from skstu/tdbrg's
2026-10-08 working tree (base commit c6a252d86d6a9f0f89675c4798431a0f0f070872;
node FIX files also contained uncommitted work): projects/sdks/client/src/node_client.cpp,
projects/sdks/server/src/tls_node.hpp, projects/sdks/server/src/uv_loop.hpp,
and projects/com/node_fix.hpp. It retains tdbrg's Apache-2.0 terms.
Changes unify client/server TLS, add bounded asynchronous writes, inject session
profiles/stores and keep all tdbrg product and trading policy in the consumer.
QuickFIX, libuv and OpenSSL are external dependencies with their own licenses.
The existing QuickFIX 1.16.0 revision 3 vcpkg overlay is retained in
cmake/vcpkg/ports/quickfix, including its NonStopSession constructor patch;
its imported vcpkg scaffolding retains LICENSE.vcpkg. libnet's TCP wrapper retains its existing MIT notice.
