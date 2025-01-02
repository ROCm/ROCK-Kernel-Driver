%global pkg amdgpu
%global kernel kernel version
%define pkg_version 6.8.7
%define osdb_version 1798298
%define anolis_release 1

%global debug_package %{nil}

Name:        kmod-%{pkg}
Version:     %(echo %{kernel} | sed -E 's/-/~/g; s/\.(an|al)[0-9]+$//g')
Release:     %{pkg_version}_%{osdb_version}~%{anolis_release}%{?dist}
Summary:     The amdgpu Linux kernel driver

License:     GPLv2 and Redistributable, no modification permitted
URL:         http://www.amd.com/
Source0:     kmod-%{pkg}-%{pkg_version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
Requires:       kernel >= %{kernel}

%description
The AMD display driver kernel module in DKMS format for AMD graphics S/W

%prep
%autosetup -n kmod-%{pkg}-%{pkg_version} -p1

%build
pushd src
%{__make} KERNELVER=%(uname -r)
popd

%install
mkdir -p %{buildroot}/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu
%{__install} -D -t %{buildroot}/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu src/amddrm_buddy.ko src/amddrm_ttm_helper.ko src/scheduler/amd-sched.ko src/ttm/amdttm.ko src/amd/amdxcp/amdxcp.ko src/amd/amdgpu/amdgpu.ko src/amd/amdkcl/amdkcl.ko

# Make .ko objects temporarily executable for automatic stripping
find %{buildroot}/lib/modules -type f -name \*.ko -exec chmod u+x \{\} \+

# Generate depmod.conf
%{__install} -d %{buildroot}/%{_sysconfdir}/depmod.d/
for kmod in $(find %{buildroot}/lib/modules/%{kernel}.%{_arch}/extra -type f -name \*.ko -printf "%%P\n" | sort)
do
    echo "override $(basename $kmod .ko) * weak-updates/$(dirname $kmod)" >> %{buildroot}/%{_sysconfdir}/depmod.d/%{pkg}.conf
    echo "override $(basename $kmod .ko) * extra/$(dirname $kmod)" >> %{buildroot}/%{_sysconfdir}/depmod.d/%{pkg}.conf
done

%clean
%{__rm} -rf %{buildroot}

%post
depmod -a > /dev/null 2>&1

if [ -x "/usr/sbin/weak-modules" ]; then
    printf '%s\n' "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdgpu.ko" | /usr/sbin/weak-modules --no-initramfs --add-modules
    printf '%s\n' "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdkcl.ko" | /usr/sbin/weak-modules --no-initramfs --add-modules
    printf '%s\n' "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdxcp.ko" | /usr/sbin/weak-modules --no-initramfs --add-modules
    printf '%s\n' "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amddrm_buddy.ko" | /usr/sbin/weak-modules --no-initramfs --add-modules
    printf '%s\n' "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amddrm_ttm_helper.ko" | /usr/sbin/weak-modules --no-initramfs --add-modules
    printf '%s\n' "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amd-sched.ko" | /usr/sbin/weak-modules --no-initramfs --add-modules
    printf '%s\n' "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdttm.ko" | /usr/sbin/weak-modules --no-initramfs --add-modules
fi
dracut -f --kver %{kernel}.%{_arch}

%preun
echo "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdgpu.ko" >> /var/run/rpm-%{pkg}-modules.list
echo "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdkcl.ko" >> /var/run/rpm-%{pkg}-modules.list
echo "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdxcp.ko" >> /var/run/rpm-%{pkg}-modules.list
echo "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amddrm_buddy.ko" >> /var/run/rpm-%{pkg}-modules.list
echo "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amddrm_ttm_helper.ko" >> /var/run/rpm-%{pkg}-modules.list
echo "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amd-sched.ko" >> /var/run/rpm-%{pkg}-modules.list
echo "/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdttm.ko" >> /var/run/rpm-%{pkg}-modules.list

%postun
depmod -a > /dev/null 2>&1

if [ -x "/usr/sbin/weak-modules" ]; then
    modules=( $(cat /var/run/rpm-%{pkg}-modules.list) )
    printf '%s\n' "${modules[@]}" | /usr/sbin/weak-modules --no-initramfs --remove-modules
fi
rm /var/run/rpm-%{pkg}-modules.list
dracut -f --kver %{kernel}.%{_arch}

%files
/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdgpu.ko
/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdkcl.ko
/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdxcp.ko
/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amddrm_buddy.ko
/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amddrm_ttm_helper.ko
/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amd-sched.ko
/lib/modules/%{kernel}.%{_arch}/extra/drivers/gpu/drm/amdgpu/amdttm.ko
%defattr(644,root,root,755)
%license licenses
%config(noreplace) %{_sysconfdir}/depmod.d/%{pkg}.conf

%changelog
* Thu Jul 18 2024 Bob Zhou <Bob.Zhou@amd.com> - 6.8.7-1798298
-
