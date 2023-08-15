# 网络设计

网络io与io多路复用select/poll/epoll，以及基于事件驱动的reactor，HTTP服务器实现


multi_io.c：网络io与io多路复用select/poll/epoll

reactor.c：基于事件驱动的reactor网络设计模型

reactor_HTTP.c：基于reactor的HTTP服务器实现—— read html and read image

reactor_KV.c：基于reactor的HTTP服务器实现—— Use key-value pairs to store HTTP packets
