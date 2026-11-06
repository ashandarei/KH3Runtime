#pragma once

namespace kh3ap
{
    // A visit spans all private 0.2 rooms and character changes. Call only after
    // the current native inventory and (on exit) AP receipts are ready.
    class Kh02ComboMasterVisit
    {
      public:
        template <class Ensure, class Remove>
        void update(bool in_kh02, bool received_from_ap, Ensure&& ensure, Remove&& remove)
        {
            if (in_kh02)
            {
                if (!inside)
                {
                    inside = true;
                    entry_pending = true;
                }
                if (entry_pending && ensure())
                {
                    entry_pending = false;
                }
            }
            else if (inside && (received_from_ap || remove()))
            {
                inside = false;
                entry_pending = false;
            }
        }

        auto active() const -> bool
        {
            return inside;
        }

        auto entry_complete() const -> bool
        {
            return inside && !entry_pending;
        }

      private:
        bool inside{};
        bool entry_pending{};
    };
} // namespace kh3ap
