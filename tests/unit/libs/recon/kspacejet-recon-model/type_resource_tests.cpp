#include "kspacejet/recon/model.hpp"
#include "kspacejet/recon/type_registry.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] ksj::recon::TypeDescriptorSpec valid_frame_type() {
  return {
    .type_ref = "ksj.kspace-frame",
    .payload_kind = ksj::recon::PayloadKind::buffer_handle,
    .element_type = ksj::recon::ElementType::complex_float32,
    .rank = 3,
    .dimensions = {"channel", "ky", "kx"},
    .layout = ksj::recon::LayoutKind::channel_major_contiguous,
    .strides = ksj::recon::StrideKind::canonical,
    .explicit_byte_strides = {},
    .allowed_memory_domains = {ksj::recon::TypeMemoryDomain::cuda_device, ksj::recon::TypeMemoryDomain::host_pinned},
    .min_alignment_bytes = 64,
    .mutability = ksj::recon::PayloadMutability::immutable_after_publish,
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

TEST(KSpaceJetReconModelTypeDescriptor, CanonicalizesMemoryDomainSetAndRequiresExactMatch) {
  const auto created = ksj::recon::TypeDescriptor::create(valid_frame_type());
  ASSERT_TRUE(created.ok()) << created.status();

  const auto& descriptor = created.value();
  EXPECT_EQ("ksj.kspace-frame", descriptor.type_ref().value());
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

  auto different_alignment = valid_frame_type();
  different_alignment.min_alignment_bytes = 128;
  const auto identity_incompatible = ksj::recon::TypeDescriptor::create(different_alignment);
  ASSERT_TRUE(identity_incompatible.ok()) << identity_incompatible.status();
  EXPECT_FALSE(descriptor.exactly_matches(identity_incompatible.value()));
  EXPECT_NE(descriptor.type_identity_digest(), identity_incompatible.value().type_identity_digest());
}

TEST(KSpaceJetReconModelTypeDescriptor, MatchesTheGeneratedRegistryIdentityVectors) {
  const auto frame = ksj::recon::types::kspace_frame();
  ASSERT_TRUE(frame.ok()) << frame.status();
  EXPECT_EQ("ksj.kspace-frame", frame.value().type_ref().value());
  EXPECT_EQ("sha256:c8cd515591bd7b7049e4b4dd5c207a3977bb6f46053e9d397e0d68475cc00dd9",
            frame.value().type_identity_digest().value());

  const auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(image.ok()) << image.status();
  EXPECT_EQ("ksj.image-frame", image.value().type_ref().value());
  EXPECT_EQ("sha256:66d13ed43407ba9dd449327f62a752f8137d328bad2620f56b817bf2140b3586",
            image.value().type_identity_digest().value());

  const auto control = ksj::recon::types::control_message();
  ASSERT_TRUE(control.ok()) << control.status();
  EXPECT_EQ("ksj.control-message", control.value().type_ref().value());
  EXPECT_EQ("sha256:63727083de1ab6915ffe5b4dbebedfda207f61a9e8b7fdf9c8e796a5b38f8f91",
            control.value().type_identity_digest().value());

  const auto acquisition = ksj::recon::types::ismrmrd_acquisition();
  ASSERT_TRUE(acquisition.ok()) << acquisition.status();
  EXPECT_EQ("sha256:c465cbdd3119a30e991e3a608d381a44833d5b6ddb148f55120b880af3a4d99b",
            acquisition.value().type_identity_digest().value());

  const auto waveform = ksj::recon::types::ismrmrd_waveform();
  ASSERT_TRUE(waveform.ok()) << waveform.status();
  EXPECT_EQ("sha256:cf317a25802af2032ab944c81eb6f6c8527822984704c20e742e1cb4b75f1696",
            waveform.value().type_identity_digest().value());

  const auto public_image = ksj::recon::types::ismrmrd_image();
  ASSERT_TRUE(public_image.ok()) << public_image.status();
  EXPECT_EQ("sha256:ff79d7b100e93e57c628d0e4f25d3d0fbd7d5fd56f363a70259a96054a901ef9",
            public_image.value().type_identity_digest().value());
}

TEST(KSpaceJetReconModelTypeDescriptor, ResolvesAllCurrentMriDataTypeRefsWithTheirStructuralLayouts) {
  using ksj::recon::ElementType;
  using ksj::recon::LayoutKind;

  struct TypeExpectation {
    std::string_view type_ref;
    ElementType element_type;
    ksj::recon::Quantity rank;
    LayoutKind layout;
  };
  constexpr std::array expectations{
    TypeExpectation{"ksj.kspace-frame", ElementType::complex_float32, 3U, LayoutKind::channel_major_contiguous},
    TypeExpectation{"ksj.noise-calibration-frame", ElementType::complex_float32, 2U,
                    LayoutKind::channel_major_contiguous},
    TypeExpectation{"ksj.noise-model", ElementType::complex_float32, 2U, LayoutKind::row_major_contiguous},
    TypeExpectation{"ksj.phase-reference-frame", ElementType::complex_float32, 3U,
                    LayoutKind::channel_major_contiguous},
    TypeExpectation{"ksj.phase-model", ElementType::complex_float32, 2U, LayoutKind::channel_major_contiguous},
    TypeExpectation{"ksj.coil-compression-basis", ElementType::complex_float32, 2U, LayoutKind::row_major_contiguous},
    TypeExpectation{"ksj.coil-image-frame", ElementType::complex_float32, 3U, LayoutKind::row_major_contiguous},
    TypeExpectation{"ksj.complex-image-frame", ElementType::complex_float32, 2U, LayoutKind::row_major_contiguous},
    TypeExpectation{"ksj.sensitivity-map", ElementType::complex_float32, 3U, LayoutKind::row_major_contiguous},
    TypeExpectation{"ksj.noncartesian-kspace-frame", ElementType::complex_float32, 2U,
                    LayoutKind::channel_major_contiguous},
    TypeExpectation{"ksj.trajectory-frame", ElementType::float32, 2U, LayoutKind::row_major_contiguous},
  };

  for (const auto& expectation : expectations) {
    const auto descriptor = ksj::recon::types::resolve(expectation.type_ref);
    ASSERT_TRUE(descriptor.ok()) << expectation.type_ref << ": " << descriptor.status();
    EXPECT_EQ(expectation.type_ref, descriptor.value().type_ref().value());
    EXPECT_EQ(expectation.element_type, descriptor.value().element_type());
    EXPECT_EQ(expectation.rank, descriptor.value().rank());
    EXPECT_EQ(expectation.layout, descriptor.value().layout());
  }

  const auto kspace = ksj::recon::types::kspace_frame();
  ASSERT_TRUE(kspace.ok()) << kspace.status();
  EXPECT_EQ((std::vector<std::string>{"channel", "ky", "kx"}), kspace.value().dimensions());

  const auto coil_images = ksj::recon::types::coil_image_frame();
  ASSERT_TRUE(coil_images.ok()) << coil_images.status();
  EXPECT_EQ((std::vector<std::string>{"ky", "kx", "channel"}), coil_images.value().dimensions());

  EXPECT_FALSE(ksj::recon::types::resolve("ksj.unknown-frame").ok());
}

TEST(KSpaceJetReconModelTypeDescriptor, RejectsIncompleteOrAmbiguousDescriptors) {
  auto missing_type_ref = valid_frame_type();
  missing_type_ref.type_ref.clear();
  EXPECT_FALSE(ksj::recon::TypeDescriptor::create(missing_type_ref).ok());

  auto non_canonical_type_ref = valid_frame_type();
  non_canonical_type_ref.type_ref = "ksj.kspace-frame/";
  EXPECT_FALSE(ksj::recon::TypeDescriptor::create(non_canonical_type_ref).ok());

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

TEST(KSpaceJetReconModelResourceVector, SeparatesDomainsAndCanonicalizesDeviceIdentity) {
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

TEST(KSpaceJetReconModelResourceVector, EnforcesThePublicDeviceIdentityUnicodeLengthBound) {
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

TEST(KSpaceJetReconModelResourceVector, CapacityRequiresEveryDomainAndHostHierarchy) {
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
