#pragma once

#include <vector>
#include <unordered_map>
#include <stdint.h>
#include <memory>
#include <utility>
#include <stdexcept>

namespace daglib {
    struct Resource {
        uint32_t id;
        double resource_size;
    };

    struct DagNode {
        double task_complexity;

        std::vector<uint32_t> resource_dependenies;
        std::vector<Resource> resource_production;
    };

    class DagTask {
    public:
        void add_node(DagNode node) {
            for (const auto& res : node.resource_production) {
                if (resource_producer.find(res.id) != resource_producer.end()) {
                    throw std::runtime_error("There can't be two different nodes which produce same resource");
                }
            }
            nodes.emplace_back(std::move(node));
            for (const auto& res : nodes.back().resource_production) {
                resource_producer[res.id] = nodes.size() - 1;
            }
        }

        void precalculate_dependencies() {
            calculated_dependencies.resize(nodes.size());

            calculated_reverse_dependencies.clear();
            calculated_reverse_dependencies.resize(nodes.size());

            for (size_t i = 0; i < nodes.size(); ++i) {
                calculated_dependencies[i].clear();
                calculated_dependencies[i].reserve(nodes[i].resource_dependenies.size());
                for (uint32_t res_id : nodes[i].resource_dependenies) {
                    if (resource_producer.find(res_id) == resource_producer.end()) {
                        throw std::runtime_error("Node depends on unproducable resource");
                    }
                    calculated_dependencies[i].push_back(resource_producer.at(res_id));
                    calculated_reverse_dependencies[resource_producer.at(res_id)].emplace_back(i, res_id);
                }
            }
        }

        const std::vector<DagNode>& get_nodes() const {
            return nodes;
        }

        const std::vector<std::vector<std::pair<size_t, uint32_t>>>& get_reverse_dependencies() const {
            return calculated_reverse_dependencies;
        }

    private:
        std::vector<DagNode> nodes;
        std::vector<std::vector<size_t>> calculated_dependencies;
        std::vector<std::vector<std::pair<size_t, uint32_t>>> calculated_reverse_dependencies;
        std::unordered_map<uint32_t, size_t> resource_producer;
    };
}