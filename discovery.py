"""mDNS 广播。

只广播"这台机器上跑着一个 Agent 控制台"，不广播任何会话内容。
设备发现之后仍需令牌才能读数据。
"""
import socket

from zeroconf import ServiceInfo, Zeroconf

SERVICE = "_agentdash._tcp.local."


def local_ip() -> str:
    """取本机在局域网上的出口 IP。

    连一个外部地址但不真的发包，内核会挑出正确的出口网卡 —— 这样能避开
    Hyper-V / WSL 那些虚拟网卡。
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("223.5.5.5", 80))
        return s.getsockname()[0]
    finally:
        s.close()


def txt_properties(host_id: str, hostname: str) -> dict:
    return {"v": "1", "host": hostname, "id": host_id}


def register(port: int, host_id: str, hostname: str, ip: str | None = None):
    ip = ip or local_ip()
    info = ServiceInfo(
        SERVICE,
        f"{hostname}.{SERVICE}",
        addresses=[socket.inet_aton(ip)],
        port=port,
        properties=txt_properties(host_id, hostname),
        server=f"{hostname}.local.",
    )
    zc = Zeroconf(interfaces=[ip])          # 显式绑定局域网口，别广播到虚拟网卡
    zc.register_service(info)
    return (zc, info)


def unregister(handle) -> None:
    zc, info = handle
    try:
        zc.unregister_service(info)
    finally:
        zc.close()
