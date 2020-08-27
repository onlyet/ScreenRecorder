#include "Capture.h"
#include "MemoryPool.h"
#include "MediaFormat.h"
#include "glog/logging.h"
#include <QTime>

Capture::Capture()
{
}

Capture::~Capture()
{
    m_oper_lck.lock();
    if (m_mempool)
    {
        delete m_mempool;
    }
    m_oper_lck.unlock();
}

bool Capture::startCapture()
{
    m_oper_lck.lock();
    if (!m_init)
    {
        LOG(ERROR) << "Capture Thread not init!";
        return false;
    }

    m_start = true;
    m_oper_lck.unlock();

    LOG(INFO) << "start Capture Thread";
    start();    // ����qt�߳�

    return true;
}

bool Capture::stopCapture()
{
    m_oper_lck.lock();
    if (!m_init)
    {
        LOG(WARNING) << "Capture Thread not init";
        return false;
    }
    clean();    // �������� 
    m_start = false;
    m_oper_lck.unlock();

    LOG(INFO) << "stop Capture Thread";
    wait();             // �ȴ�qt�߳��˳�

    return true;
}

void Capture::pushData(FrameData * p)
{
    if (!p || !p->data) { return; }
    std::lock_guard<std::mutex> lck(m_data_lck);

    m_frames.push(p);
}

FrameData * Capture::getData()
{
    std::lock_guard<std::mutex> lck(m_data_lck);

    FrameData *data = nullptr;
    if (!m_frames.empty())
    {
        data = m_frames.front();
        m_frames.pop();
    }

    return data;
}

// ���ﲻ��Ҫ�����ɣ�m_data_lckӦ��ֻ������m_frames����
void Capture::freeData(FrameData * p)
{
    std::lock_guard<std::mutex> lck(m_data_lck);

    if (m_mempool)
    {
        m_mempool->freeMemory(p);
    }
}

void Capture::clean()
{
    std::lock_guard<std::mutex> lck(m_data_lck);

    while (!m_frames.empty())
    {
        FrameData *p = m_frames.front();
        if (m_mempool)
        {
            m_mempool->freeMemory(p);
        }
        m_frames.pop();
    }
}

void Capture::run()
{
    QTime time;
    int wait_time = 0;
    int use_time = 0;

    while (m_start)
    {
        time.restart();     // ��ʱ����0�������ȡһ֡����ʱ��
        m_oper_lck.lock();
        if (!m_start)   // double check
        {
            m_oper_lck.unlock();
            break;
        }

        FrameData *frame = m_mempool->allocMemory();
        if (!frame || !frame->data)
        {
            m_oper_lck.unlock();
            LOG(DETAIL) << "cache buff full, wait 1ms, cache size:" << m_cacheSize;
            msleep(1);   // �������������ȴ�10ms
            continue;
        }

        if (!captureData(frame->data)) // ��ȡһ֡����
        {
            m_mempool->freeMemory(frame);
            frame = nullptr;
            m_oper_lck.unlock();
            LOG(DETAIL) << "captureData failed!";
            msleep(1);
            continue;
        }

        frame->type = m_type;
        frame->timestamp = QDateTime::currentMSecsSinceEpoch(); // ��¼ʱ���
        pushData(frame);                                        // д�뻺�����

        // ��ץȡ��֡�ʿ���FPS,ͬ��ץȡ��֡��
        use_time = time.restart();
        wait_time = syncTimeMs(use_time);
        wait_time = wait_time > 0 ? wait_time : 1;

        m_oper_lck.unlock();

        LOG(DEBUG) << "use_time:" << use_time << ", wait_time:" << wait_time;
        msleep(wait_time);
    }
}