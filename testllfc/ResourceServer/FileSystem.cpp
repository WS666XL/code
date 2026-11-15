#include "FileSystem.h"
#include "ConfigMgr.h"
#include "CSession.h"
#include "base64.h"

FileWorker::FileWorker() :_b_stop(false)
{
	_work_thread = std::thread([this]() {
		while (!_b_stop) {
			std::unique_lock<std::mutex> lock(_mtx);
			_cv.wait(lock, [this]() {
				if (_b_stop) {
					return true;
				}

				if (_task_que.empty()) {
					return false;
				}

				return true;

			});

			if (_b_stop) {
				return;
			}

			auto task = _task_que.front();
			task_callback(task);
			_task_que.pop();
		}

	});
}

FileWorker::~FileWorker()
{
	_b_stop = true;
	_cv.notify_one();
	_work_thread.join();
}

void FileWorker::PostTask(std::shared_ptr<FileTask> task)
{
	{
		std::lock_guard<std::mutex> lock(_mtx);
		_task_que.push(task);
	}

	_cv.notify_one();
}

//真正的落盘逻辑：base64 解码后按 seq 追加写入文件。
//seq==1 打开文件并清空（新建），其余追加。
//因为同一文件的分块保证在同一线程串行处理，不会出现写乱序
void FileWorker::task_callback(std::shared_ptr<FileTask> task)
{
	// 解码 base64 得到原始二进制分块
	std::string decoded = base64_decode(task->_file_data);

	auto file_path = ConfigMgr::Inst().GetFileOutPath();
	auto file_path_str = (file_path / task->_name).string();
	auto last = task->_last;
	std::ofstream outfile;
	//第一个包：打开文件，如果存在则清空，不存在则创建
	if (task->_seq == 1) {
		outfile.open(file_path_str, std::ios::binary | std::ios::trunc);
	}
	else {
		// 保存为文件
		outfile.open(file_path_str, std::ios::binary | std::ios::app);
	}


	if (!outfile) {
		std::cerr << "无法打开文件进行写入。" << std::endl;
		return;
	}

	outfile.write(decoded.data(), decoded.size());
	if (!outfile) {
		std::cerr << "写入文件失败。" << std::endl;
		return;
	}

	outfile.close();
	if (last) {
		std::cout << "文件已成功保存为: " << task->_name
			<< " total_size=" << task->_total_size << std::endl;
	}
}

FileSystem::~FileSystem()
{

}

void FileSystem::PostMsgToQue(shared_ptr<FileTask> msg, int index)
{
	_file_workers[index]->PostTask(msg);
}

FileSystem::FileSystem()
{
	for (int i = 0; i < FILE_WORKER_COUNT; i++) {
		_file_workers.push_back(std::make_shared<FileWorker>());
	}
}
