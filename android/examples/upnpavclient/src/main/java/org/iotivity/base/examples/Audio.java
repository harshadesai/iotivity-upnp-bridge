/*
 * //******************************************************************
 * //
 * // Copyright 2018 Intel Corporation All Rights Reserved.
 * //
 * //-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 * //
 * // Licensed under the Apache License, Version 2.0 (the "License");
 * // you may not use this file except in compliance with the License.
 * // You may obtain a copy of the License at
 * //
 * //      http://www.apache.org/licenses/LICENSE-2.0
 * //
 * // Unless required by applicable law or agreed to in writing, software
 * // distributed under the License is distributed on an "AS IS" BASIS,
 * // WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * // See the License for the specific language governing permissions and
 * // limitations under the License.
 * //
 * //-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 */

package org.iotivity.base.examples;

import android.os.Parcel;
import android.os.Parcelable;

import org.iotivity.base.OcException;
import org.iotivity.base.OcRepresentation;

/**
 * Audio
 *
 * This class is used by UpnpAvClientActivity to create an object representation of a remote audio resource
 * and update the values depending on the server response
 */
public class Audio extends Service implements Parcelable {

    public static final String OIC_TYPE_AUDIO = "oic.r.audio";
    public static final String OCF_OIC_URI_PREFIX_AUDIO = "/ocf/audio/";
    public static final String UPNP_OIC_URI_PREFIX_AUDIO = "/upnp/audio/";
    public static final String UPNP_OIC_URI_PREFIX_AUDIO_NM_HREF = "/upnp/audio/RenderingControl/";

    public static final String MUTE_KEY = "mute";
    public static final boolean DEFAULT_MUTE = false;

    public static final String VOLUME_KEY = "volume";
    public static final int DEFAULT_VOLUME = 0;

    private boolean mMute;
    private int mVolume;
    private boolean mIsInitialized;

    public Audio() {
        super();
        mMute = DEFAULT_MUTE;
        mVolume = DEFAULT_VOLUME;
    }

    public void setOcRepresentation(OcRepresentation rep) throws OcException {
        super.setOcRepresentation(rep);

        boolean muteIsInitialized = false;
        boolean volumeIsInitialized = false;

        if (rep.hasAttribute(MUTE_KEY)) {
            Object obj = rep.getValue(MUTE_KEY);
            if (obj instanceof Boolean) {
                mMute = (Boolean) obj;
                muteIsInitialized = true;
            }
        }
        if (rep.hasAttribute(VOLUME_KEY)) {
            Object obj = rep.getValue(VOLUME_KEY);
            if (obj instanceof Integer) {
                mVolume = (Integer) obj;
                volumeIsInitialized = true;
            }
        }

        mIsInitialized = (muteIsInitialized && volumeIsInitialized);
    }

    public OcRepresentation getOcRepresentation() throws OcException {
        OcRepresentation rep = super.getOcRepresentation();
        rep.setValue(MUTE_KEY, mMute);
        rep.setValue(VOLUME_KEY, mVolume);
        return rep;
    }

    public boolean isMute() {
        return mMute;
    }

    public void setMute(boolean mute) {
        mMute = mute;
    }

    public int getVolume() {
        return mVolume;
    }

    public void setVolume(int volume) {
        mVolume = volume;
    }

    public boolean isInitialized() {
        return mIsInitialized;
    }

    @Override
    public String toString() {
        return "[" + super.toString() +
                ", " + "initialized: " + mIsInitialized +
                ", " + MUTE_KEY + ": " + mMute +
                ", " + VOLUME_KEY + ": " + mVolume + "]";
    }

    // Parcelable implementation
    public Audio(Parcel in) {
        readFromParcel(in);
    }

    public static final Parcelable.Creator CREATOR = new Parcelable.Creator() {
        public Audio createFromParcel(Parcel in) {
            return new Audio(in);
        }

        public Audio[] newArray(int size) {
            return new Audio[size];
        }
    };

    public void writeToParcel(Parcel dest, int flags) {
        dest.writeString(getName());
        dest.writeString(getUri());
        dest.writeInt(getVolume());
        dest.writeByte((byte) (isMute() ? 1 : 0));
        dest.writeByte((byte) (isInitialized() ? 1 : 0));
    }

    private void readFromParcel(Parcel in) {
        setName(in.readString());
        setUri(in.readString());
        setVolume(in.readInt());
        setMute(in.readByte() != 0);
        mIsInitialized = in.readByte() != 0;
    }

    public int describeContents() {
        return 0;
    }
}
