#include "shared/kh02/kh02_control_protocol.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace kh02control;
    Identity identity{101, 202, 303, 404};
    identity.nonce.fill('a');
    Request request;
    request.identity = identity;
    request.generation = 1;
    request.client_pid = 101;
    request.operation = Operation::inspect_assets;
    assert(validate(request, identity, 101) == Status::accepted);
    assert(!ready_for_request(Operation::inspect_assets, false, false));
    assert(ready_for_request(Operation::inspect_assets, true, false));
    assert(!ready_for_request(Operation::inspect, true, false));
    assert(ready_for_request(Operation::inspect, true, true));
    assert(!ready_for_request(Operation::stream, false, false));
    assert(ready_for_request(Operation::stream, true, false)); // Queued, never executed off the parked thread.
    auto invalid_operation = request;
    invalid_operation.operation = static_cast<Operation>(5);
    assert(validate(invalid_operation, identity, 101) == Status::invalid);
    request.operation = Operation::stream;
    assert(assign(request.movie, "movie/dw026_movie.usm"));
    assert(assign(request.shared_name, "Local\\KH02Movie_101_202_0123456789abcdef0123456789abcdef"));
    assert(validate(request, identity, 101) == Status::accepted);
    assert(validate(request, identity, 102) == Status::invalid);
    auto changed = request;
    changed.client_pid = 909;
    assert(validate(changed, identity, 909) == Status::wrong_session);
    changed = request;
    changed.identity.catalog_tag++;
    assert(validate(changed, identity, 101) == Status::wrong_session);
    changed = request;
    changed.identity.session_generation++;
    assert(validate(changed, identity, 101) == Status::wrong_session);
    changed = request;
    changed.identity.nonce[0] = 'b';
    assert(validate(changed, identity, 101) == Status::wrong_session);
    changed = request;
    changed.protocol++;
    assert(validate(changed, identity, 101) == Status::invalid);
    changed = request;
    changed.bytes--;
    assert(validate(changed, identity, 101) == Status::invalid);
    changed = request;
    changed.movie.fill('x');
    assert(validate(changed, identity, 101) == Status::invalid);
    changed = request;
    changed.flags = 4;
    assert(validate(changed, identity, 101) == Status::invalid);
    assert(!assign(changed.movie, std::string(128, 'x')));
    assert(!assign(changed.movie, std::string_view("x\0y", 3)));

    Mailbox mailbox;
    assert(mailbox.accept(request) == Status::accepted);
    assert(mailbox.has_pending());
    assert(mailbox.accept(request) == Status::accepted); // Lost acknowledgement is safe to retransmit.
    changed = request;
    assert(assign(changed.movie, "gear_a.usm"));
    assert(mailbox.accept(changed) == Status::duplicate); // Same generation cannot replace content.
    changed = request;
    changed.generation++;
    assert(mailbox.accept(changed) == Status::busy);
    assert(mailbox.take()->generation == request.generation);
    assert(!mailbox.take());
    assert(mailbox.accept(changed) == Status::busy); // Taken still means decoding.
    mailbox.complete();
    assert(mailbox.accept(request) == Status::accepted);
    assert(!mailbox.has_pending()); // Replaying a completed request never decodes it twice.
    assert(mailbox.accept(changed) == Status::accepted);
    assert(mailbox.take()->generation == changed.generation);

    Reply reply;
    reply.status = Status::accepted;
    reply.identity = request.identity;
    reply.generation = request.generation;
    assert(valid_reply(reply, request));
    reply.generation++;
    assert(!valid_reply(reply, request));
    std::cout << "KH02 control identity, bounds, replay and one-decoder lifecycle passed\n";
}
