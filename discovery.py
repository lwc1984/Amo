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
    except OSError:
        return "127.0.0.1"        # 没网时别让托盘菜单渲染崩掉
    finally:
        s.close()


def txt_properties(host_id: str, hostname: str) -> dict:
    return {"v": "1", "host": hostname, "id": host_id}


class Broadcast:
    """mDNS 广播的生命周期。

    宿主 IP 会变（DHCP 续约、Wi-Fi 切网线、VPN 开关），而这正是当初选 mDNS、
    没让设备写死地址的理由。所以广播不能是"启动时注册一次"就完事 ——
    要盯着出口 IP，变了就重新注册。
    """

    def __init__(self, port: int, host_id: str, hostname: str):
        self.port = port
        self.host_id = host_id
        self.hostname = hostname
        self.ip: str | None = None
        self._zc = None
        self._info = None

    def start(self, ip: str | None = None) -> None:
        ip = ip or local_ip()
        info = ServiceInfo(
            SERVICE,
            f"{self.hostname}.{SERVICE}",
            addresses=[socket.inet_aton(ip)],
            port=self.port,
            properties=txt_properties(self.host_id, self.hostname),
            server=f"{self.hostname}.local.",
        )
        zc = Zeroconf(interfaces=[ip])      # 显式绑定局域网口，别广播到虚拟网卡
        try:
            zc.register_service(info)
        except Exception:
            zc.close()                      # 注册失败也要收回已打开的组播 socket
            raise
        self._zc, self._info, self.ip = zc, info, ip

    def stop(self) -> None:
        if self._zc is None:
            return
        try:
            self._zc.unregister_service(self._info)
        finally:
            self._zc.close()
            self._zc = None
            self._info = None
            self.ip = None

    def refresh(self, ip: str | None = None) -> bool:
        """出口 IP 变了就重新注册。返回是否真的重新注册过。"""
        ip = ip or local_ip()
        if ip == self.ip:
            return False
        self.stop()
        self.start(ip)
        return True
