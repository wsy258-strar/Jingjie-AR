export function isMobileDevice(navigatorObject = globalThis.navigator) {
  if (typeof navigatorObject?.userAgentData?.mobile === "boolean")
    return navigatorObject.userAgentData.mobile;
  return /Android|iPhone|iPad|iPod/i.test(navigatorObject?.userAgent || "");
}

export class GyroController {
  constructor({
    adapter,
    deviceOrientation = globalThis.DeviceOrientationEvent,
    deviceMotion = globalThis.DeviceMotionEvent,
    notifyUnavailable = true,
    onDenied = () => {}
  } = {}) {
    this.adapter = adapter;
    this.deviceOrientation = deviceOrientation;
    this.deviceMotion = deviceMotion;
    this.notifyUnavailable = Boolean(notifyUnavailable);
    this.onDenied = typeof onDenied === "function" ? onDenied : () => {};
    this.permissionSources = [deviceOrientation, deviceMotion].filter(
      (source) => typeof source?.requestPermission === "function"
    );
    this.permissionRequired = this.permissionSources.length > 0;
    this.permissionRequested = false;
    this.permissionGranted = !this.permissionRequired;
    this.permissionRequest = null;
    this.pluginAvailable = false;
    this.denialNotified = false;
    this.suspensions = new Set();
    this.gyroEnabled = false;
    this.destroyed = false;
  }

  isAvailable() {
    return this.pluginAvailable;
  }

  notifyDeniedOnce() {
    if (this.denialNotified) return false;
    this.denialNotified = true;
    this.onDenied();
    return true;
  }

  enableIfAllowed() {
    if (this.destroyed || !this.permissionGranted || this.suspensions.size || !this.isAvailable()) return false;
    if (!this.gyroEnabled) {
      this.adapter.enableGyro();
      this.gyroEnabled = true;
    }
    return true;
  }

  async requestFromGesture() {
    if (this.destroyed) return false;
    if (!this.permissionRequired) return this.enableIfAllowed();
    if (this.permissionRequest) {
      await this.permissionRequest;
      return this.enableIfAllowed();
    }
    if (this.permissionRequested) return this.enableIfAllowed();

    this.permissionRequested = true;
    const requests = this.permissionSources.map((source) => {
      try {
        return Promise.resolve(source.requestPermission.call(source));
      } catch (error) {
        return Promise.reject(error);
      }
    });
    this.permissionRequest = Promise.all(requests).then(
      (permissions) => permissions.every((permission) => permission === "granted"),
      () => false
    );
    this.permissionGranted = await this.permissionRequest;
    if (!this.permissionGranted) {
      this.notifyDeniedOnce();
      return false;
    }
    return this.enableIfAllowed();
  }

  async autoEnable() {
    if (this.permissionRequired) return false;
    return this.enableIfAllowed();
  }

  handlePluginState(state) {
    if (this.destroyed) return false;
    if (state === "available") {
      this.pluginAvailable = true;
      return this.enableIfAllowed();
    }
    if (state === "unavailable") {
      this.pluginAvailable = false;
      if (this.gyroEnabled) {
        this.adapter.disableGyro();
        this.gyroEnabled = false;
      }
      if (this.notifyUnavailable) this.notifyDeniedOnce();
      return false;
    }
    if (state === "enabled") {
      this.gyroEnabled = true;
      return true;
    }
    if (state === "disabled") this.gyroEnabled = false;
    return false;
  }

  suspend(reason) {
    this.suspensions.add(reason);
    if (this.gyroEnabled) {
      this.adapter.disableGyro();
      this.gyroEnabled = false;
    }
  }

  resume(reason) {
    this.suspensions.delete(reason);
    return this.enableIfAllowed();
  }

  destroy() {
    if (this.destroyed) return;
    this.destroyed = true;
    this.suspensions.clear();
    if (this.gyroEnabled) this.adapter.disableGyro();
    this.gyroEnabled = false;
  }
}
