"""为局域网浏览器节点提供 HTTPS/WSS 转发与本地证书。"""
import argparse
import asyncio
import ipaddress
import ssl
from datetime import datetime, timedelta, timezone
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


ROOT = Path(__file__).resolve().parents[1]
CERT_DIR = ROOT / ".local-certs"


def write_key(path, key):
    path.write_bytes(key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
        serialization.NoEncryption()))


def certificates(ips):
    CERT_DIR.mkdir(exist_ok=True)
    ca_cert_path = CERT_DIR / "lan-root-ca.crt"
    ca_key_path = CERT_DIR / "lan-root-ca.key"
    now = datetime.now(timezone.utc)
    reuse_ca = ca_cert_path.exists() and ca_key_path.exists()
    if reuse_ca:
        ca_cert = x509.load_pem_x509_certificate(ca_cert_path.read_bytes())
        ca_key = serialization.load_pem_private_key(ca_key_path.read_bytes(), password=None)
        try:
            ca_cert.extensions.get_extension_for_class(x509.SubjectKeyIdentifier)
        except x509.ExtensionNotFound:
            reuse_ca = False
    if not reuse_ca:
        ca_key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Multi-device Audio LAN CA")])
        ca_cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
                   .public_key(ca_key.public_key()).serial_number(x509.random_serial_number())
                   .not_valid_before(now - timedelta(days=1))
                   .not_valid_after(now + timedelta(days=1825))
                   .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
                   .add_extension(x509.SubjectKeyIdentifier.from_public_key(
                       ca_key.public_key()), critical=False)
                   .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                        key_encipherment=False, data_encipherment=False, key_agreement=False,
                        key_cert_sign=True, crl_sign=True, encipher_only=False,
                        decipher_only=False), critical=True)
                   .sign(ca_key, hashes.SHA256()))
        write_key(ca_key_path, ca_key)
        ca_cert_path.write_bytes(ca_cert.public_bytes(serialization.Encoding.PEM))
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, str(ips[0]))])
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(ca_cert.subject)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - timedelta(days=1))
            .not_valid_after(now + timedelta(days=365))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()),
                           critical=False)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(
                ca_key.public_key()), critical=False)
            .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ip) for ip in ips]),
                           critical=False)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]),
                           critical=False)
            .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                key_encipherment=True, data_encipherment=False, key_agreement=False,
                key_cert_sign=False, crl_sign=False, encipher_only=False,
                decipher_only=False), critical=True)
            .sign(ca_key, hashes.SHA256()))
    write_key(CERT_DIR / "lan-server.key", key)
    (CERT_DIR / "lan-server.crt").write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    return ca_cert_path, CERT_DIR / "lan-server.crt", CERT_DIR / "lan-server.key"


async def forward(source, destination):
    try:
        while data := await source.read(65536):
            destination.write(data)
            await destination.drain()
    finally:
        destination.close()


async def serve_connection(reader, writer, backend_port):
    try:
        backend_reader, backend_writer = await asyncio.open_connection("127.0.0.1", backend_port)
    except OSError:
        writer.close()
        await writer.wait_closed()
        return
    tasks = [asyncio.create_task(forward(reader, backend_writer)),
             asyncio.create_task(forward(backend_reader, writer))]
    done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
    for task in pending:
        task.cancel()
    await asyncio.gather(*tasks, return_exceptions=True)
    writer.close()
    backend_writer.close()


async def main():
    parser = argparse.ArgumentParser(description="局域网 HTTPS/WSS 转发")
    parser.add_argument("--ip", action="append", required=True, help="Windows 的局域网 IP，可重复")
    parser.add_argument("--port", type=int, default=17900)
    parser.add_argument("--backend-port", type=int, default=17890)
    args = parser.parse_args()
    ips = [ipaddress.ip_address(value) for value in args.ip]
    ca_path, server_cert, server_key = certificates(ips)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(server_cert, server_key)
    server = await asyncio.start_server(
        lambda r, w: serve_connection(r, w, args.backend_port), "0.0.0.0", args.port,
        ssl=context)
    print(f"iPad 先安装并信任证书：{ca_path}")
    for ip in ips:
        print(f"https://{ip}:{args.port}/")
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
