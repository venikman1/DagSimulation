#include "simgrid/s4u.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

#include "dag.hpp"

XBT_LOG_NEW_DEFAULT_CATEGORY(s4u_async_waitall, "Messages specific for this s4u example");

daglib::DagTask global_dag_task;

enum class MessageType : uint32_t {
    FINISH = 0,
    SEND_RESOURCE = 1,
    DO_TASK = 2,
    DONE = 3
};

struct Message {
    MessageType type;
    void* payload;

    static char* message_type_str[];
};

char* Message::message_type_str[] = {"FINISH", "SEND_RESOURCE", "DO_TASK", "DONE"};

struct Task {
    std::vector<uint32_t> dependencies;
    std::vector<daglib::Resource> production;
    double complexity;
    std::vector<std::pair<std::string, uint32_t>> send_to;
};

class Master {
    long init_res;  /* - Inititation resource */
    long receivers_count; /* - number of receivers */

public:
    explicit Master(std::vector<std::string> args) {
        xbt_assert(args.size() == 3, "Expecting 2 parameters from the XML deployment file but got %zu", args.size());
        init_res = std::stol(args[1]);
        receivers_count = std::stol(args[2]);
    }
    void operator()() {
        XBT_INFO("Start working");
        std::vector<simgrid::s4u::CommPtr> pending_comms;

        std::vector<std::string> workers;
        for (int i = 0; i < receivers_count; i++)
            workers.push_back(std::string("receiver-") + std::to_string(i));

        std::vector<simgrid::s4u::Mailbox *> mboxes;
        for (int i = 0; i < receivers_count; i++)
            mboxes.push_back(simgrid::s4u::Mailbox::by_name(workers[i]));
        simgrid::s4u::Mailbox *master_mbox = simgrid::s4u::Mailbox::by_name("master");

        std::vector<size_t> assigned_worker = {
            0,
            1,
            0,
            1,
            2,
            2
        };

        std::unordered_map<size_t, bool> is_worker_busy;

        int current_assignment = 0;
        
        while (current_assignment < assigned_worker.size()) {
            size_t current_worker = assigned_worker[current_assignment];
            while (is_worker_busy[current_worker]) {
                XBT_INFO("%lu worker is busy, trying to wait", current_worker);
                const Message* msg = static_cast<Message*>(master_mbox->get());
                XBT_INFO("Get message with status %s", Message::message_type_str[(size_t) msg->type]);
                size_t* worker_id = static_cast<size_t*>(msg->payload);
                is_worker_busy[*worker_id] = false;
                XBT_INFO("Worker with id %lu has done his work", *worker_id);
                delete worker_id;
                delete msg;
            }
            Message* msg = new Message;
            msg->type = MessageType::DO_TASK;

            Task* task = new Task;
            task->complexity = global_dag_task.get_nodes()[current_assignment].task_complexity;
            task->dependencies = global_dag_task.get_nodes()[current_assignment].resource_dependenies;
            task->production = global_dag_task.get_nodes()[current_assignment].resource_production;
            for (const auto& node_res : global_dag_task.get_reverse_dependencies()[current_assignment])
                if (assigned_worker[node_res.first] != assigned_worker[current_assignment])
                    task->send_to.push_back(std::make_pair(workers[assigned_worker[node_res.first]], node_res.second));
            msg->payload = task;

            pending_comms.push_back(
                mboxes[current_worker]->put_async(msg, sizeof(msg))
            );
            is_worker_busy[current_worker] = true;
            XBT_INFO("Send '%s' to %s", "DO_TASK", workers[current_worker].c_str());
            ++current_assignment;
        }
        XBT_INFO("Waiting for end of working");
        for (const auto& a : is_worker_busy) {
            XBT_INFO("STATUS %lu - %d", a.first, a.second);
        }
        for (int i = 0; i < receivers_count; i++) {   
            while (is_worker_busy[i]) {
                XBT_INFO("%lu worker is busy, trying to wait", i);
                const Message* msg = static_cast<Message*>(master_mbox->get());
                XBT_INFO("Get message with status %s", Message::message_type_str[(size_t) msg->type]);
                size_t* worker_id = static_cast<size_t*>(msg->payload);
                is_worker_busy[*worker_id] = false;
                XBT_INFO("Worker with id %lu has done his work", *worker_id);
                delete worker_id;
                delete msg;
            }
        }
        for (int i = 0; i < receivers_count; i++)
        {   
            XBT_INFO("Send FINISH to 'receiver-%d'", i);
            Message* msg = new Message;
            msg->type = MessageType::FINISH;
            msg->payload = nullptr;
            simgrid::s4u::CommPtr comm = mboxes[i]->put_async(msg, 4);
            pending_comms.push_back(comm);
        }
        XBT_INFO("Done dispatching all messages");

        simgrid::s4u::Comm::wait_all(&pending_comms);

        XBT_INFO("Goodbye now!");
    }
};

/* Worker actor expects 1 argument: its ID */
class Worker {
    size_t id;
    simgrid::s4u::Mailbox *mbox;
    std::vector<daglib::Resource> resources; /* - resources which this machine has */
    std::unordered_map<uint32_t, size_t> resource_index;

    bool check_resources(const std::vector<uint32_t>& dependencies) {
        for (uint32_t id : dependencies) {
            if (resource_index.find(id) == resource_index.end()) {
                return false;
            }
        }
        return true;
    }

    void add_resource(daglib::Resource res) {
        XBT_INFO("Get resource with id %u", res.id);
        resources.push_back(std::move(res));
        resource_index[resources.back().id] = resources.size() - 1;
    }

    void receive_resource() {
        const Message* msg = static_cast<Message*>(mbox->get());
        xbt_assert(msg->type == MessageType::SEND_RESOURCE, "Expecting getting resource now");
        daglib::Resource* res = static_cast<daglib::Resource*>(msg->payload);
        add_resource(*res);
        
        delete res;
        delete msg;
    }

public:
    explicit Worker(std::vector<std::string> args) {
        xbt_assert(args.size() == 2, "Expecting one parameter from the XML deployment file but got %zu", args.size());
        std::string mboxName = std::string("receiver-") + args[1];
        id = std::stoul(args[1]);
        mbox = simgrid::s4u::Mailbox::by_name(mboxName);
    }

    void operator()() {
        XBT_INFO("Start working");
        simgrid::s4u::Mailbox *master_mbox = simgrid::s4u::Mailbox::by_name("master");
        bool working = true;
        std::vector<simgrid::s4u::CommPtr> pending_comms;

        while (working) {
            const Message* msg = static_cast<Message*>(mbox->get());
            XBT_INFO("Get message with status %s", msg->message_type_str[(size_t) msg->type]);
            if (msg->type == MessageType::FINISH) {
                working = false;
            } else if (msg->type == MessageType::DO_TASK) {
                Task* task = static_cast<Task*>(msg->payload);
                while (!check_resources(task->dependencies)) {
                    XBT_INFO("Can't execute task, trying to wait for resources");
                    receive_resource();
                }
                XBT_INFO("Execute task");
                simgrid::s4u::this_actor::execute(task->complexity);
                for (const auto& res : task->production)
                    add_resource(res);
                std::vector<simgrid::s4u::CommPtr> pending_comms;
                for (const auto& send_to : task->send_to) {
                    const daglib::Resource& sending_resource = resources[resource_index.at(send_to.second)];
                    
                    Message* msg = new Message;
                    msg->type = MessageType::SEND_RESOURCE;
                    msg->payload = new daglib::Resource(sending_resource);
                    XBT_INFO("Sending resource with id %u to %s", sending_resource.id, send_to.first.c_str());
                    pending_comms.push_back(
                        simgrid::s4u::Mailbox::by_name(send_to.first)->put_async(msg, sending_resource.resource_size)
                    );
                }
                simgrid::s4u::Comm::wait_all(&pending_comms);
                pending_comms.clear();
                delete task;

                Message* msg = new Message;
                msg->type = MessageType::DONE;
                msg->payload = new size_t(id);
                master_mbox->put(msg, 4);
            } else if (msg->type == MessageType::SEND_RESOURCE) {
                daglib::Resource* res = static_cast<daglib::Resource*>(msg->payload);
                add_resource(*res);

                delete res;
            }
            delete msg;
        }
    }
};

int main(int argc, char *argv[])
{
    xbt_assert(argc > 2, "Usage: %s platform_file deployment_file\n", argv[0]);

    daglib::Resource diff_test_result = {0, 1e6};
    daglib::Resource beta1_info = {1, 256};
    daglib::Resource beta2_info = {2, 256};
    daglib::Resource perfomance1_test_result = {3, 1e9};
    daglib::Resource perfomance2_test_result = {4, 1e9};
    daglib::Resource final_report = {5, 1e3};

    daglib::DagNode init_beta1 = {1e6, {}, {beta1_info}};
    daglib::DagNode init_beta2 = {1e6, {}, {beta2_info}};
    daglib::DagNode perfomance_test1 = {243e9, {beta1_info.id}, {perfomance1_test_result}};
    daglib::DagNode perfomance_test2 = {243e9, {beta2_info.id}, {perfomance2_test_result}};
    daglib::DagNode diff = {16e9, {perfomance1_test_result.id, perfomance2_test_result.id}, {diff_test_result}};
    daglib::DagNode report = {1e6, {diff_test_result.id}, {final_report}};
    
    global_dag_task.add_node(init_beta1);
    global_dag_task.add_node(init_beta2);
    global_dag_task.add_node(perfomance_test1);
    global_dag_task.add_node(perfomance_test2);
    global_dag_task.add_node(diff);
    global_dag_task.add_node(report);

    global_dag_task.precalculate_dependencies();

    simgrid::s4u::Engine e(&argc, argv);
    e.register_actor<Master>("master");
    e.register_actor<Worker>("worker");

    e.load_platform(argv[1]);
    e.load_deployment(argv[2]);
    e.run();

    return 0;
}
