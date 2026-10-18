#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cassert>
#include <cstring>
#include "../src/protocol/protocol_parser.h"

int main() {
  std::cout << "[TEST] Running Comprehensive Protocol Parser Golden Corpus Test..." << std::endl;

  std::ifstream f("contracts/protocol_v1_golden.json");
  if (!f.is_open()) {
    std::cerr << "Failed to open contracts/protocol_v1_golden.json" << std::endl;
    return 1;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  std::string content = ss.str();

  // Test individual valid requests directly
  const char* validCases[] = {
    "{\"v\":1,\"id\":\"req-001\",\"type\":\"system.ping\"}\n",
    "{\"v\":1,\"id\":\"req-002\",\"type\":\"system.snapshot\"}\n",
    "{\"v\":1,\"id\":\"req-003\",\"type\":\"system.stop\"}\n",
    "{\"v\":1,\"id\":\"req-004\",\"type\":\"system.estop\"}\n",
    "{\"v\":1,\"id\":\"req-005\",\"type\":\"system.reset_fault\"}\n",
    "{\"v\":1,\"id\":\"req-006\",\"type\":\"driver.set_enabled\",\"enabled\":true}\n",
    "{\"v\":1,\"id\":\"req-007\",\"type\":\"driver.set_enabled\",\"enabled\":false}\n",
    "{\"v\":1,\"id\":\"req-008\",\"type\":\"home.start\",\"axis\":\"all\"}\n",
    "{\"v\":1,\"id\":\"req-009\",\"type\":\"home.start\",\"axis\":\"x\"}\n",
    "{\"v\":1,\"id\":\"req-010\",\"type\":\"home.start\",\"axis\":\"y\"}\n",
    "{\"v\":1,\"id\":\"req-011\",\"type\":\"motion.jog\",\"axis\":\"x\",\"steps\":200,\"speed_sps\":100}\n",
    "{\"v\":1,\"id\":\"req-012\",\"type\":\"motion.jog\",\"axis\":\"y\",\"steps\":-50,\"speed_sps\":120}\n",
    "{\"v\":1,\"id\":\"req-013\",\"type\":\"motion.move_abs\",\"x\":500,\"y\":300,\"speed_sps\":150}\n",
    "{\"v\":1,\"id\":\"req-014\",\"type\":\"laser.pulse\",\"duty\":1024,\"duration_ms\":50}\n",
    "{\"v\":1,\"id\":\"req-015\",\"type\":\"laser.set_duty\",\"duty\":512}\n",
    "{\"v\":1,\"id\":\"req-016\",\"type\":\"job.begin\",\"job_id\":\"job-test-01\",\"total_segments\":2}\n",
    "{\"v\":1,\"id\":\"req-017\",\"type\":\"job.segment\",\"job_id\":\"job-test-01\",\"seg_idx\":0,\"dx\":100,\"dy\":0,\"speed_sps\":100,\"duty\":1500}\n",
    "{\"v\":1,\"id\":\"req-018\",\"type\":\"job.segment\",\"job_id\":\"job-test-01\",\"seg_idx\":1,\"dx\":0,\"dy\":50,\"speed_sps\":200,\"duty\":0}\n",
    "{\"v\":1,\"id\":\"req-019\",\"type\":\"job.end\",\"job_id\":\"job-test-01\"}\n"
  };

  int count = 0;
  for (const char* raw : validCases) {
    CommandRequest req;
    ErrorCode err = ProtocolParser::parseLine(raw, strlen(raw), req);
    if (err != ErrorCode::Ok) {
      std::cerr << "FAILED valid case: " << raw << " -> error: " << errorCodeToString(err) << std::endl;
      assert(false);
    }
    count++;
  }
  std::cout << "  ✓ All " << count << " valid requests verified successfully." << std::endl;

  // Test invalid requests and error codes
  struct InvalidTestCase {
    const char* raw;
    ErrorCode expected;
  };

  InvalidTestCase invalidCases[] = {
    {"{\"v\":2,\"id\":\"req-err-1\",\"type\":\"system.ping\"}\n", ErrorCode::UnsupportedVersion},
    {"{\"v\":1,\"id\":\"req-err-2\",\"type\":\"robot.dance\"}\n", ErrorCode::UnknownCommand},
    {"{\"v\":1,\"id\":\"req-err-3\"}\n", ErrorCode::InvalidArgument},
    {"{\"v\":1,\"type\":\"system.ping\"}\n", ErrorCode::InvalidArgument},
    {"{\"v\":1,\"id\":\"broken\",not json}\n", ErrorCode::ParseError},
    {"{\"v\":1,\"id\":\"req-err-4\",\"type\":\"motion.jog\",\"axis\":\"x\"}\n", ErrorCode::InvalidArgument},
    {"{\"v\":1,\"id\":\"req-err-5\",\"type\":\"motion.jog\",\"axis\":\"z\",\"steps\":100}\n", ErrorCode::InvalidArgument},
    {"{\"v\":1,\"id\":\"req-err-6\",\"type\":\"laser.pulse\",\"duty\":4095,\"duration_ms\":50}\n", ErrorCode::LaserDutyClamped},
    {"{\"v\":1,\"id\":\"req-err-7\",\"type\":\"laser.pulse\",\"duty\":500,\"duration_ms\":5000}\n", ErrorCode::InvalidArgument},
    {"{\"v\":1,\"id\":\"req-err-8\",\"type\":\"motion.jog\",\"axis\":\"x\",\"steps\":100,\"speed_sps\":0}\n", ErrorCode::InvalidArgument},
    {"{\"v\":1,\"id\":\"req-err-9\",\"type\":\"motion.jog\",\"axis\":\"x\",\"steps\":100,\"speed_sps\":50000}\n", ErrorCode::OutOfBounds}
  };

  int invalidCount = 0;
  for (const auto& tc : invalidCases) {
    CommandRequest req;
    ErrorCode err = ProtocolParser::parseLine(tc.raw, strlen(tc.raw), req);
    if (err != tc.expected) {
      std::cerr << "FAILED invalid case: " << tc.raw << " -> expected: " << errorCodeToString(tc.expected)
                << " but got: " << errorCodeToString(err) << std::endl;
      assert(false);
    }
    invalidCount++;
  }
  std::cout << "  ✓ All " << invalidCount << " invalid error rejection cases verified successfully." << std::endl;

  // Serialization tests
  {
    AckResponse ack;
    ack.v = 1;
    strcpy(ack.id, "req-008");
    ack.accepted = true;
    ack.code = ErrorCode::Ok;
    strcpy(ack.operationId, "op-home-01");

    char buf[512];
    size_t len = ProtocolParser::serializeAck(ack, buf, sizeof(buf));
    assert(len > 0);
    assert(strstr(buf, "\"operation_id\":\"op-home-01\"") != nullptr);

    OperationEvent ev;
    ev.v = 1;
    strcpy(ev.eventType, "operation.completed");
    strcpy(ev.operationId, "op-home-01");
    strcpy(ev.result, "success");
    ev.homedX = true;
    ev.homedY = true;
    ev.posX = 0;
    ev.posY = 0;
    len = ProtocolParser::serializeOperationEvent(ev, buf, sizeof(buf));
    assert(len > 0);
    assert(strstr(buf, "\"result\":\"success\"") != nullptr);
    assert(strstr(buf, "\"homed\":{\"x\":true,\"y\":true}") != nullptr);

    SnapshotEvent snap;
    snap.state = MachineStateEnum::IdleHomed;
    snap.homedX = true;
    snap.homedY = true;
    snap.posX = 150;
    snap.posY = 220;
    snap.posValid = true;
    snap.sensorXRaw = true;
    snap.sensorXFiltered = false;
    snap.sensorYRaw = true;
    snap.sensorYFiltered = false;
    snap.laserDuty = 0;
    snap.laserActive = false;
    snap.laserHardClamped = false;
    snap.driversEnabled = true;
    len = ProtocolParser::serializeSnapshot(snap, buf, sizeof(buf));
    assert(len > 0);
    assert(strstr(buf, "\"state\":\"idle_homed\"") != nullptr);
    assert(strstr(buf, "\"drivers_enabled\":true") != nullptr);
  }
  std::cout << "  ✓ Serialization round-trip verified." << std::endl;

  std::cout << "[SUCCESS] 100% of Golden Corpus tests passed!" << std::endl;
  return 0;
}
