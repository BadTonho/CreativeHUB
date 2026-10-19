#pragma once
#include "composition_document.h"
#include <unordered_map>
#include <unordered_set>
namespace motion::model {
// Immutable relation snapshot. O(layers) validation/order, without recursion.
// Parenting is resolved separately and never introduces a pixel dependency.
class TrackMatteGraph final {
public:
    explicit TrackMatteGraph(const std::vector<CompositionLayer>&);
    [[nodiscard]] bool empty() const noexcept {return participants_.empty();}
    [[nodiscard]] bool participates(LayerId id) const {return participants_.contains(id);}
    [[nodiscard]] std::optional<TrackMatteBinding> binding(LayerId id) const {return nodes_.at(index_.at(id)).matte;}
    [[nodiscard]] const std::vector<LayerId>& order() const noexcept {return order_;}
    [[nodiscard]] std::vector<LayerId> eligibleSources(LayerId) const;
    struct Participation {bool needed=false;bool composite=false;};
    // Result matches input document order. Inactive sources need no pixels.
    [[nodiscard]] std::vector<Participation> plan(const std::vector<CompositionLayer>&,
        std::int64_t composition_frame,LayerId mask_view=0) const;
private:
    struct Node {LayerId id;LayerKind kind;std::optional<TrackMatteBinding> matte;};
    std::vector<Node> nodes_;
    std::unordered_map<LayerId,std::size_t> index_;
    std::unordered_map<LayerId,std::vector<LayerId>> consumers_;
    std::unordered_set<LayerId> participants_;
    std::vector<LayerId> order_;
};
}
