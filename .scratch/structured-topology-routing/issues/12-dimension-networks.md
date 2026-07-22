状态: 已完成
阻塞于: 11

# 统一识别维度网络

把 Ring、Mesh、Torus 和 ND Dimension FullMesh 统一识别为 `dimension-network`。输出每个
节点的坐标、每个维度的 `line`、`ring` 或 `fullMesh` 连接关系，以及对应的物理端口组。

验收：覆盖一维 Ring、非方形二维 Mesh、二维和三维 Torus、三维 Dimension FullMesh、混合
维度、并行链路、打乱节点 ID 和边顺序；缺边、增边、跨维换边和端口错误必须拒绝或局部
回退。所有采用的规则与默认最短路逐查询项等价。

完成情况：实现统一 `dimension-network`，按 `line`、`ring`、`fullMesh` 维度生成模板并使用
度数分布、角点离心率、`could_be_isomorphic` 和 VF2++ 恢复坐标。已覆盖 Ring、非方形 Mesh、
二维 Torus、三维 Dimension FullMesh、混合维度、节点和边重排及并行物理链路。`32 x 32`
Torus 的发现和物理验证实测约 `0.033 s`。
