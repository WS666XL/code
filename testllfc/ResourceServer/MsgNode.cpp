#include "MsgNode.h"
RecvNode::RecvNode(int max_len, short msg_id):MsgNode(max_len),
_msg_id(msg_id){

}


SendNode::SendNode(const char* msg, int max_len, short msg_id):MsgNode(max_len + HEAD_TOTAL_LEN)
, _msg_id(msg_id){
	//头部布局：2 字节 id + 4 字节长度，均转网络字节序（大端），客户端 QDataStream 大端解析
	short msg_id_host = boost::asio::detail::socket_ops::host_to_network_short(msg_id);
	memcpy(_data, &msg_id_host, HEAD_ID_LEN);
	//4 字节长度：文件分块包体可能很大，所以用 long 转换
	int max_len_host = boost::asio::detail::socket_ops::host_to_network_long(max_len);
	memcpy(_data + HEAD_ID_LEN, &max_len_host, HEAD_DATA_LEN);
	memcpy(_data + HEAD_ID_LEN + HEAD_DATA_LEN, msg, max_len);
}
