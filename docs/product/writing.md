# 固件仓库怎么写、写多少

产品故事和 Type 是同一条路径。写法以 Type 仓库为准：
[怎么写产品功能](https://github.com/Listener-ai-Macau/Listener-Type/blob/master/docs/product/writing.md)

本仓库只补固件侧的篇幅：

| 文档 | 写多少 | 写什么 |
| --- | --- | --- |
| `README.md` / `README.zh.md` | 80–120 行 | 这是买回去那台键盘上的软件；30 秒配对；链到 Type |
| `docs/product/features.md` | 80–120 行 | 键、灯、配对、唤醒、OTA、和 Type 的分工 |
| `docs/release/<版本>.md` | 40–80 行 | 这版键盘做什么、OTA 哈希、和 Type 配对；细节链回 Type |

版本只在 `master` 打 `v主.次.修订` tag，不要开 `release/<版本>` 分支。
| `docs/features/firmware-feature-map.md` | 不限 | GPIO、HID、验收；不要贴进 README |

不要在本仓库再写一遍插入光标、风格包、词库。点到 Type 的功能页即可。
构建命令放 README 下半，不要压过「为什么买这台键盘」。
