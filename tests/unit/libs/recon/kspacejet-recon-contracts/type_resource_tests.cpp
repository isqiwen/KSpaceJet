#include "kspacejet/recon/contracts.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <utility>

namespace {

constexpr auto kPayloadDigest = "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr auto kMetadataDigest = "sha256:cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34";

[[nodiscard]] ksj::recon::TypeDescriptorSpec valid_frame_type() {
  return {
    .type_id = "ksj.kspace-frame",
    .revision = 1,
    .payload_schema_digest = kPayloadDigest,
    .payload_kind = ksj::recon::PayloadKind::buffer_handle,
    .element_type = ksj::recon::ElementType::complex_int16,
    .rank = 3,
    .dimensions = {"channel", "ky", "kx"},
    .layout = ksj::recon::LayoutKind::channel_major_contiguous,
    .strides = ksj::recon::StrideKind::canonical,
    .explicit_byte_strides = {},
    .allowed_memory_domains = {ksj::recon::TypeMemoryDomain::cuda_device, ksj::recon::TypeMemoryDomain::host_pinned},
    .min_alignment_bytes = 64,
    .mutability = ksj::recon::PayloadMutability::immutable_after_publish,
    .metadata_schema_digest = kMetadataDigest,
  };
}

[[nodiscard]] ksj::recon::ResourceVectorSpec valid_resources() {
  return {
    .host_normal_bytes = 4096,
    .host_pinned_bytes = 1024,
    .host_hugepage_bytes = 2048,
    .shared_host_bytes = 512,
    .spool_bytes = 8192,
    .transport_bytes = 256,
    .descriptor_count = 32,
    .async_token_count = 8,
    .cpu_leaf_permits = 4,
    .backend_gang_permits = 2,
    .provider_private_permits = 1,
    .io_slots = 3,
    .devices =
      {
        {.device_id = "cuda:1", .device_bytes = 4096, .gpu_stream_slots = 4, .copy_engine_slots = 2},
        {.device_id = "cuda:0", .device_bytes = 8192, .gpu_stream_slots = 8, .copy_engine_slots = 3},
      },
  };
}

TEST(KSpaceJetReconContractsTypeDescriptor, CanonicalizesMemoryDomainSetAndRequiresExactMatch) {
  const auto created = ksj::recon::TypeDescriptor::create(valid_frame_type());
  ASSERT_TRUE(created.ok()) << created.status();

  const auto& descriptor = created.value();
  EXPECT_EQ("ksj.kspace-frame", descriptor.type_id());
  EXPECT_EQ(3U, descriptor.rank());
  EXPECT_EQ(64U, descriptor.min_alignment_bytes());
  ASSERT_EQ(2U, descriptor.allowed_memory_domains().size());
  EXPECT_EQ(ksj::recon::TypeMemoryDomain::host_pinned, descriptor.allowed_memory_domains()[0]);
  EXPECT_EQ(ksj::recon::TypeMemoryDomain::cuda_device, descriptor.allowed_memory_domains()[1]);

  auto same_specification = valid_frame_type();
  same_specification.allowed_memory_domains = {ksj::recon::TypeMemoryDomain::host_pinned,
                                               ksj::recon::TypeMemoryDomain::cuda_device};
  const auto same = ksj::recon::TypeDescriptor::create(same_specification);
  ASSERT_TRUE(same.ok()) << same.status();
  EXPECT_TRUE(descriptor.exactly_matches(same.value()));

  auto incompatible_specification = valid_frame_type();
  incompatible_specification.layout = ksj::recon::LayoutKind::row_major_contiguous;
  const auto incompatible = ksj::recon::TypeDescriptor::create(incompatible_specification);
  ASSERT_TRUE(incompatible.ok()) << incompatible.status();
  EXPECT_FALSE(descriptor.exactly_matches(incompatible.value()));
}

TEST(KSpaceJetReconContractsTypeDescriptor, RejectsIncompleteOrAmbiguousDescriptors) {
  auto bad_alignment = valid_frame_type();
  bad_alignment.min_alignment_bytes = 48;
  EXPECT_FALSE(ksj::recon::TypeDescriptor::create(bad_alignment).ok());

  auto duplicate_domain = valid_frame_type();
  duplicate_domain.allowed_memory_domains = {ksj::recon::TypeMemoryDomain::host_normal,
                                             ksj::recon::TypeMemoryDomain::host_normal};
  EXPECT_FALSE(ksj::recon::TypeDescriptor::create(duplicate_domain).ok());

  auto bad_strides = valid_frame_type();
  bad_strides.strides = ksj::recon::StrideKind::explicit_byte_strides;
  bad_strides.explicit_byte_strides = {128, 8};
  EXPECT_FALSE(ksj::recon::TypeDescriptor::create(bad_strides).ok());

  auto bad_dimensions = valid_frame_type();
  bad_dimensions.dimensions = {"channel", "channel", "kx"};
  EXPECT_FALSE(ksj::recon::TypeDescriptor::create(bad_dimensions).ok());
}

TEST(KSpaceJetReconContractsResourceVector, SeparatesDomainsAndCanonicalizesDeviceIdentity) {
  const auto created = ksj::recon::ResourceVector::create(valid_resources());
  ASSERT_TRUE(created.ok()) << created.status();

  const auto& resources = created.value();
  EXPECT_EQ(7680U, resources.host_total_bytes());
  ASSERT_EQ(2U, resources.devices().size());
  EXPECT_EQ("cuda:0", resources.devices()[0].device_id());
  EXPECT_EQ(8192U, resources.devices()[0].device_bytes());
  EXPECT_NE(nullptr, resources.find_device("cuda:1"));
  EXPECT_EQ(nullptr, resources.find_device("cuda:9"));

  auto same_specification = valid_resources();
  std::swap(same_specification.devices[0], same_specification.devices[1]);
  const auto same = ksj::recon::ResourceVector::create(same_specification);
  ASSERT_TRUE(same.ok()) << same.status();
  EXPECT_TRUE(resources.exactly_matches(same.value()));

  auto duplicate_device = valid_resources();
  duplicate_device.devices[1].device_id = "cuda:1";
  EXPECT_FALSE(ksj::recon::ResourceVector::create(duplicate_device).ok());
}

TEST(KSpaceJetReconContractsResourceVector, EnforcesThePublicDeviceIdentityUnicodeLengthBound) {
  auto boundary = valid_resources();
  std::string unicode_device_id;
  for (std::size_t index = 0U; index < 255U; ++index) {
    unicode_device_id.append("\xC3\xA9");
  }
  boundary.devices.front().device_id = unicode_device_id;
  EXPECT_TRUE(ksj::recon::ResourceVector::create(boundary).ok());

  unicode_device_id.append("\xC3\xA9");
  boundary.devices.front().device_id = unicode_device_id;
  EXPECT_FALSE(ksj::recon::ResourceVector::create(boundary).ok());
}

TEST(KSpaceJetReconContractsResourceVector, CapacityRequiresEveryDomainAndHostHierarchy) {
  auto capacity_domains = valid_resources();
  capacity_domains.host_normal_bytes = 8192;
  capacity_domains.host_pinned_bytes = 2048;
  capacity_domains.host_hugepage_bytes = 4096;
  capacity_domains.shared_host_bytes = 1024;
  capacity_domains.devices[0].device_bytes = 8192;
  capacity_domains.devices[1].device_bytes = 16384;

  const auto capacity = ksj::recon::ResourceVectorCapacity::create({
    .domains = capacity_domains,
    .host_total_cap_bytes = 16384,
  });
  ASSERT_TRUE(capacity.ok()) << capacity.status();

  const auto demand = ksj::recon::ResourceVector::create(valid_resources());
  ASSERT_TRUE(demand.ok()) << demand.status();
  EXPECT_TRUE(capacity.value().can_admit(demand.value()));

  auto too_much_pinned = valid_resources();
  too_much_pinned.host_pinned_bytes = 4096;
  const auto too_much_pinned_vector = ksj::recon::ResourceVector::create(too_much_pinned);
  ASSERT_TRUE(too_much_pinned_vector.ok()) << too_much_pinned_vector.status();
  EXPECT_FALSE(capacity.value().can_admit(too_much_pinned_vector.value()));

  auto missing_device = valid_resources();
  missing_device.devices[1].device_id = "cuda:7";
  const auto missing_device_vector = ksj::recon::ResourceVector::create(missing_device);
  ASSERT_TRUE(missing_device_vector.ok()) << missing_device_vector.status();
  EXPECT_FALSE(capacity.value().can_admit(missing_device_vector.value()));

  const auto invalid_hierarchy = ksj::recon::ResourceVectorCapacity::create({
    .domains = capacity_domains,
    .host_total_cap_bytes = 1000,
  });
  EXPECT_FALSE(invalid_hierarchy.ok());
}

} // namespace
